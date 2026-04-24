#include <string.h>
#include <Core/Defines.h>
#include <Core/Debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <Sys/File/filesys.h>
#include "nx_init.h"
#include "chars.h"
#include "texture.h"
#include "render.h"

#include "Com/texturedecode.h"

namespace NxWn32
{


/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
sTexture::sTexture()
{
	// Create texture
	glGenTextures(1, &GLTexture);
	glBindTexture(GL_TEXTURE_2D, GLTexture);

	// Enable mipmaps
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
sTexture::~sTexture()
{
	// Delete texture
	if (Data != nullptr)
		delete[] Data;

	if (GLFramebuffer != 0)
	{
		glDeleteFramebuffers(1, &GLFramebuffer);
		GLFramebuffer = 0;
	}
	if (GLDepthRenderbuffer != 0)
	{
		glDeleteRenderbuffers(1, &GLDepthRenderbuffer);
		GLDepthRenderbuffer = 0;
	}
	glDeleteTextures(1, &GLTexture);
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
void sTexture::Set( int pass )
{
	glActiveTexture(GL_TEXTURE0 + pass);
	glBindTexture(GL_TEXTURE_2D, GLTexture);
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
bool sTexture::SetRenderTarget( int width, int height, int depth, int z_depth )
{
	(void)depth;
	(void)z_depth;

	BaseWidth = (uint16)width;
	BaseHeight = (uint16)height;
	ActualWidth = (uint16)width;
	ActualHeight = (uint16)height;
	Levels = 1;
	TexelDepth = 32;
	PaletteDepth = 0;
	DXT = 0;
	IsRenderTarget = true;

	glBindTexture(GL_TEXTURE_2D, GLTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	// Border color = (1,1,1,0) so sampling outside the projection gives "no shadow here".
	float border[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	if (GLFramebuffer == 0)
	{
		glGenFramebuffers(1, &GLFramebuffer);
	}
	if (GLDepthRenderbuffer == 0)
	{
		glGenRenderbuffers(1, &GLDepthRenderbuffer);
	}
	glBindRenderbuffer(GL_RENDERBUFFER, GLDepthRenderbuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

	GLint prev_fbo = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, GLFramebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GLTexture, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, GLDepthRenderbuffer);
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);

	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		FILE *f = fopen("shadow_diag.log", "a");
		if (f) { fprintf(f, "SetRenderTarget: FBO incomplete status=0x%x\n", status); fclose(f); }
		return false;
	}

	FILE *f = fopen("shadow_diag.log", "a");
	if (f) { fprintf(f, "SetRenderTarget: w=%d h=%d tex=%u fbo=%u rbo=%u OK\n", width, height, GLTexture, GLFramebuffer, GLDepthRenderbuffer); fclose(f); }
	return true;
}



// Eeeek - the .img contains PS2 specific register values for bit depth.
// Use these values to convert them.
#define PSMCT32		0x00
#define PSMCT24		0x01
#define PSMCT16		0x02
#define PSMCT16S	0x0A
#define PS_GPU24	0x12
#define PSMT8		0x13
#define PSMT4		0x14
#define PSMT8H		0x1B
#define PSMT4HL		0x24
#define PSMT4HH		0x2C
#define PSMZ32		0x30
#define PSMZ24		0x31
#define PSMZ16		0x32
#define PSMZ16S		0x3A



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
static bool is_power_of_two( uint32 a )
{
	if( a == 0 )
	{
		return false;
	}
	return (( a & ( a - 1 )) == 0 );
}


// Upload Data to GLTexture
void sTexture::Upload()
{
	// Bind texture
	glBindTexture(GL_TEXTURE_2D, GLTexture);

	// Set mipmaps minimum and maximum
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, Levels - 1);

	// Set texture data
	uint8 *datap = Data;

	GLsizei base_width = BaseWidth;
	GLsizei base_height = BaseHeight;
	
	for (uint8 mip_level = 0; mip_level < Levels; mip_level++)
	{
		// Write to texture
		glTexImage2D(GL_TEXTURE_2D, mip_level, GL_RGBA, base_width, base_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, datap);

		// Shift to next mip level
		datap += (base_width * base_height * 4);
		base_width >>= 1;
		base_height >>= 1;
	}
}


/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
sTexture *LoadTexture( const char *p_filename )
{
	struct sIMGHeader
	{
		uint32	version;
		uint32	checksum;
		uint32	width;
		uint32	height;
		uint32	bit_depth;
		uint32	clut_bit_depth;
		uint16	original_width;
		uint16	original_height;
		uint32	palette_data_size;
	};

	void *p_FH = File::Open( p_filename, "rb" );
	
	if( p_FH != nullptr)
	{
		// Read header.
		sIMGHeader header;
		File::Read( &header, sizeof( sIMGHeader ), 1, p_FH );
		
		// Bits per texel and palette size.
		switch( header.bit_depth )
		{
			case PSMCT32:
				header.bit_depth = 32;
				break;
			case PSMCT16:
				header.bit_depth = 16;
				break;
			case PSMT8:
				header.bit_depth = 8;
				break;
			default:
				Dbg_Assert( 0 );
		}

		// Bits per clut entry.
		if(	header.bit_depth < 16 )
		{
			switch( header.clut_bit_depth )
			{
				case PSMCT32:
					header.clut_bit_depth = 32;
					break;
				default:
					Dbg_Assert( 0 );
			}
		}
		else
		{
			header.clut_bit_depth = 0;
		}
		
		// Create the texture object.
		sTexture *p_texture = new sTexture();

		// Clamp at edges
		glBindTexture(GL_TEXTURE_2D, p_texture->GLTexture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		// Create palette if required
		uint8 pal[256][4] = {};
		if( header.clut_bit_depth > 0 )
		{
			// Read clut bitmap data.
			Dbg_Assert(header.palette_data_size <= sizeof( pal ));
			size_t len	= File::Read( pal, header.palette_data_size, 1, p_FH );
			Dbg_MsgAssert( len == header.palette_data_size, ( "Couldn't read clut from texture file %s", p_filename ));
		}

		// Read texture bitmap data
		size_t num_bytes = (((header.bit_depth / 8) * (header.width) * (header.height)) + 3) & 0xFFFFFFFC;
		uint8 *source_data = new uint8[num_bytes];
		size_t len = File::Read(source_data, num_bytes, 1, p_FH);
		Dbg_MsgAssert(len == num_bytes, ("couldn't read texture data from texture file %s", p_filename));
		File::Close(p_FH);

		// Set texture size
		p_texture->BaseWidth = (uint16)header.width;
		p_texture->BaseHeight = (uint16)header.height;
		p_texture->ActualWidth = (uint16)header.original_width;
		p_texture->ActualHeight = (uint16)header.original_height;

		p_texture->Levels = 1;

		// Convert to 32 bit
		size_t texture_size = p_texture->GetDataSize();
		uint8 *texture_data = new uint8[texture_size];

		Dbg_Assert(texture_size != 0);

		switch (header.bit_depth)
		{
			case 8:
				TextureDecode::Pal_Decode(source_data, &pal[0][0], texture_data, header.width, header.height);
				break;
			case 16:
				TextureDecode::Ps2_Decode(source_data, texture_data, header.width, header.height);
				break;
			case 32:
				TextureDecode::Long_Decode(source_data, texture_data, header.width, header.height);
				break;
			default:
				Dbg_Assert(0);
		}
		delete[] source_data;

		// Unswizzle texture
		if (is_power_of_two(header.width) && is_power_of_two(header.height))
		{
			uint8 *unswizzled_texture_data = new uint8[texture_size];
			TextureDecode::Swizzle_Decode(texture_data, unswizzled_texture_data, header.width, header.height);
			delete[] texture_data;

			// Set texture data
			p_texture->Data = unswizzled_texture_data;
		}
		else
		{
			// Set texture data
			p_texture->Data = texture_data;
		}

		// Upload texture
		p_texture->Upload();

		// Set up some member values
		p_texture->PaletteDepth	= (uint8)header.clut_bit_depth;
		p_texture->TexelDepth	= (uint8)header.bit_depth;
		p_texture->DXT			= 0;
			
		return p_texture;
	}
	return nullptr;
}


} // namespace NxWn32

