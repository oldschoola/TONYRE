/*****************************************************************************
**																			**
**			              Neversoft Entertainment.			                **
**																		   	**
**				   Copyright (C) 2000 - All Rights Reserved				   	**
**																			**
******************************************************************************
**																			**
**	Project:		SYS														**
**																			**
**	Module:			Mc			 											**
**																			**
**	File name:		memcard.cpp												**
**																			**
**	Created by:		03/06/01	-	spg										**
**																			**
**	Description:	Memcard - platform-specific implementations				**
**																			**
*****************************************************************************/

/*****************************************************************************
**							  	  Includes									**
*****************************************************************************/

#include <Core/Defines.h>
#include <Core/singleton.h>

#include <Sys/McMan.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cerrno>
#include <sys/stat.h>
#include <direct.h>
#include <io.h>
#include <windows.h>

/*****************************************************************************
**								DBG Information								**
*****************************************************************************/

namespace Mc
{

/*****************************************************************************
**								  Externals									**
*****************************************************************************/

/*****************************************************************************
**								   Defines									**
*****************************************************************************/

#define MAX_FILENAME_LENGTH	128

// Save root relative to CWD (game runs from Game/ dir).
static const char *SAVE_ROOT = "Save";

namespace
{

// Ensure the save-root directory exists; idempotent. Safe to call repeatedly.
void EnsureSaveRoot()
{
	CreateDirectoryA( SAVE_ROOT, nullptr );
}

// Strip a leading slash/backslash, swap forward slashes for backslashes, and
// prepend SAVE_ROOT. Writes into the caller-supplied buffer.
void BuildFullPath( const char *rel, char *out, size_t out_size )
{
	const char *src = rel ? rel : "";
	while ( *src == '/' || *src == '\\' )
		++src;

	_snprintf_s( out, out_size, _TRUNCATE, "%s\\%s", SAVE_ROOT, src );

	for ( char *p = out; *p; ++p )
	{
		if ( *p == '/' )
			*p = '\\';
	}
}

// Recursively remove dir + contents. Returns true on success or if dir absent.
bool RemoveTree( const char *full_path )
{
	char pattern[MAX_FILENAME_LENGTH];
	_snprintf_s( pattern, _TRUNCATE, "%s\\*", full_path );

	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA( pattern, &fd );
	if ( h != INVALID_HANDLE_VALUE )
	{
		do
		{
			if ( strcmp( fd.cFileName, "." ) == 0 || strcmp( fd.cFileName, ".." ) == 0 )
				continue;

			char child[MAX_FILENAME_LENGTH];
			_snprintf_s( child, _TRUNCATE, "%s\\%s", full_path, fd.cFileName );

			if ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
				RemoveTree( child );
			else
				DeleteFileA( child );
		}
		while ( FindNextFileA( h, &fd ) );
		FindClose( h );
	}

	BOOL rv = RemoveDirectoryA( full_path );
	return rv || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
}

// Fill a Mc::DateTime from a FILETIME (UTC).
void FillDateTime( DateTime &dt, const FILETIME &ft )
{
	FILETIME local_time;
	SYSTEMTIME sys_time;
	FileTimeToLocalFileTime( &ft, &local_time );
	FileTimeToSystemTime( &local_time, &sys_time );

	dt.m_Year    = sys_time.wYear;
	dt.m_Month   = (unsigned char)sys_time.wMonth;
	dt.m_Day     = (unsigned char)sys_time.wDay;
	dt.m_Hour    = (unsigned char)sys_time.wHour;
	dt.m_Minutes = (unsigned char)sys_time.wMinute;
	dt.m_Seconds = (unsigned char)sys_time.wSecond;
}

}

/*****************************************************************************
**								Private Types								**
*****************************************************************************/

/*****************************************************************************
**								 Private Data								**
*****************************************************************************/

DefineSingletonClass( Manager, "MemCard Manager" );

// static char cardFilenameBuffer[MAX_FILENAME_LENGTH];

/*****************************************************************************
**								 Public Data								**
*****************************************************************************/

/*****************************************************************************
**							  Private Prototypes							**
*****************************************************************************/

/*****************************************************************************
**							  Private Functions								**
*****************************************************************************/

Manager::Manager( void )
{
	for ( int i = 0; i < vMAX_PORT; ++i )
	{
		for ( int j = 0; j < vMAX_SLOT; ++j )
		{
			m_card[i][j].m_port = i;
			m_card[i][j].m_slot = j;
			m_card[i][j].m_last_error = 0;
			m_card[i][j].m_mounted_drive_letter = 0;
		}
	}

	m_hard_drive.m_port = 0;
	m_hard_drive.m_slot = 0;
	m_hard_drive.m_last_error = 0;
	m_hard_drive.SetAsHardDrive();

	EnsureSaveRoot();
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

Manager::~Manager( void )
{
}

/*****************************************************************************
**							  Public Functions								**
*****************************************************************************/

int	Manager::GetMaxSlots( int port )
{
	(void)port;
	return vMAX_SLOT;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

Card* Manager::GetCard( int port, int slot )
{
	(void)port;
	(void)slot;
	// Ignore port and slot, since on the XBox we're only using the hard drive.
	return &m_hard_drive;
}

// Skate3 code, disabled for now.
#	if 0
Card* Manager::GetHardDrive( )
{
	return &m_hard_drive;
}

const char* Manager::GetLastCardName( int port, int slot )
{
	Dbg_Assert( port < vMAX_PORT );
	Dbg_Assert( slot < vMAX_SLOT );
	
	Card *p_card = &m_card[port][slot];
	return p_card->GetLastPersonalizedName();
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
const char *Card::GetPersonalizedName()
{
	// Only get the name if the card is mounted and is not the hard drive.
	if (m_mounted_drive_letter && m_mounted_drive_letter!='u')
	{
		sprintf(mp_personalized_name,"");
		
		WCHAR p_personalized_name[MAX_MUNAME+10];
		DWORD rv=XMUNameFromDriveLetter(m_mounted_drive_letter,p_personalized_name,MAX_MUNAME);
		if (rv==ERROR_SUCCESS)
		{
			// Instead of doing a wsprintfA( mp_personalized_name, "%ls", p_personalized_name );
			// which will terminate as soon as it hits a bad char, convert each WCHAR one at a 
			// time so that all good characters come across.
			char *p_dest=mp_personalized_name;
			int count=0;
			WCHAR p_temp[2]; // A buffer for holding one WCHAR at a time for sending to wsprintfA
			p_temp[1]=0;
			const WCHAR *p_scan=p_personalized_name;
			while (*p_scan) // WCHAR strings are terminated by a 0 just like normal strings, except its a 2byte 0.
			{
				p_temp[0]=*p_scan++;
				
				char p_one_char[10];
				wsprintfA( p_one_char, "%ls", p_temp);
				// p_one_char now contains a one char string.
				
				if (count<MAX_MUNAME)
				{
					if (*p_one_char)
					{
						*p_dest=*p_one_char;
					}
					else
					{
						// Bad char, so write a ~ so that it appears as a xbox null char.
						*p_dest='~';
					}	
					++p_dest;
					++count;
				}
			}	
			*p_dest=0;
		
		
			int len=strlen(mp_personalized_name);
			
			for (int i=0; i<len; ++i)
			{
				// Force any special characters (arrows or button icons) to be displayed
				// as the xbox nullptr character by changing them to an invalid character.
				switch (mp_personalized_name[i])
				{
				case '�': case '�': case '�': case '�':
				case '�': case '�': case '�': case '�':
				case '�': case '�': case '�': case '�':
				case -1: // This is the weird lower-case-y-umlaut character.
						 // Note: The upper case y-umlaut character appears to be viewed as a
						 // terminator character by XMUNameFromDriveLetter ( = bug?)
					mp_personalized_name[i]='~';
					break;
				default:
					break;
				}	
			}
		}
		else
		{
			#ifdef __NOPT_ASSERT__
			printf("XMUNameFromDriveLetter error code = %d\n",rv);
			#endif
		}	
	}
	
	return mp_personalized_name;
}

const char *Card::GetLastPersonalizedName()
{
	return mp_personalized_name;
}

bool Card::THPS3SavesExist()
{
	if ( m_mounted_drive_letter == 0 )
	{
		return false;
	}	

	cardFilenameBuffer[0] = m_mounted_drive_letter;
	cardFilenameBuffer[1] = ':';
	cardFilenameBuffer[2] = '\\';
	cardFilenameBuffer[3] = 0;
	
	XGAME_FIND_DATA data;
	HANDLE rv=XFindFirstSaveGame(cardFilenameBuffer,&data);
	if (rv==INVALID_HANDLE_VALUE)
	{
		return false;
	}	
	else
	{
		XFindClose(rv);
		return true;
	}	
}
	
bool Card::CasParkOrReplaysExist()
{
	if ( m_mounted_drive_letter == 0 )
	{
		return false;
	}	

	char p_optpros_name[100];
	sprintf( p_optpros_name,"/Options and Pros" );
	ConvertDirectory( p_optpros_name, p_optpros_name );
	strcat(p_optpros_name,"\\");
	
	cardFilenameBuffer[0] = m_mounted_drive_letter;
	cardFilenameBuffer[1] = ':';
	cardFilenameBuffer[2] = '\\';
	cardFilenameBuffer[3] = 0;
	
	XGAME_FIND_DATA data;
	HANDLE rv=XFindFirstSaveGame(cardFilenameBuffer,&data);
	if (rv==INVALID_HANDLE_VALUE)
	{
		return false;
	}	
	
	bool cas_park_or_replays_exist=false;
	while (true)
	{
		if (stricmp(data.szSaveGameDirectory+3,p_optpros_name)==0)
		{
			// It's the optpros file
		}	
		else
		{
			// It either a cas, park or replay.
			cas_park_or_replays_exist=true;
			break;
		}	
		
		if (XFindNextSaveGame(rv,&data))
		{
		}
		else
		{
			break;
		}
	}
			
	XFindClose(rv);
	return cas_park_or_replays_exist;
}

// Returns true if the total number of cas, park or replay files is the max allowed.
bool Card::MaxFilesReached()
{
	if ( m_mounted_drive_letter == 0 )
	{
		return false;
	}	

	char p_optpros_name[100];
	sprintf( p_optpros_name,"/Options and Pros" );
	ConvertDirectory( p_optpros_name, p_optpros_name );
	strcat(p_optpros_name,"\\");
	
	cardFilenameBuffer[0] = m_mounted_drive_letter;
	cardFilenameBuffer[1] = ':';
	cardFilenameBuffer[2] = '\\';
	cardFilenameBuffer[3] = 0;
	
	XGAME_FIND_DATA data;
	HANDLE rv=XFindFirstSaveGame(cardFilenameBuffer,&data);
	if (rv==INVALID_HANDLE_VALUE)
	{
		return false;
	}	
	
	int num_files=0;
	while (true)
	{
		if (stricmp(data.szSaveGameDirectory+3,p_optpros_name)==0)
		{
			// It's the optpros file
		}	
		else
		{
			// It either a cas, park or replay.
			++num_files;
		}	
		
		if (!XFindNextSaveGame(rv,&data))
		{
			break;
		}
	}
			
	XFindClose(rv);
	
	if (num_files>=75)
	{
		return true;
	}	
	return false;
}
#	endif
	

// Note: dir_name must no longer start with a backslash, it should just be "Career2-Career" etc.
bool Card::MakeDirectory( const char* dir_name )
{
	if ( dir_name == nullptr || *dir_name == 0 )
	{
		m_last_error = vINVALID_PATH;
		return false;
	}

	EnsureSaveRoot();

	char full_path[MAX_FILENAME_LENGTH];
	BuildFullPath( dir_name, full_path, sizeof(full_path) );

	if ( CreateDirectoryA( full_path, nullptr ) )
		return true;

	DWORD err = GetLastError();
	if ( err == ERROR_ALREADY_EXISTS )
		return true;

	if ( err == ERROR_DISK_FULL )
		m_last_error = vINSUFFICIENT_SPACE;
	else
		m_last_error = vACCESS_ERROR;

	return false;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
const char *Card::ConvertDirectory( const char* dir_name )
{
	// Xbox used to mangle the save-game dir name via XCreateSaveGame. On Win32
	// we just write through to Save/<dir_name>, so the "low-level" name is the
	// same as the incoming logical name — but only if the directory actually
	// exists (callers expect nullptr when the dir has not been created yet).
	if ( dir_name == nullptr || *dir_name == 0 )
		return nullptr;

	static char output_dir[vDIRECTORY_NAME_BUF_SIZE];

	char full_path[MAX_FILENAME_LENGTH];
	BuildFullPath( dir_name, full_path, sizeof(full_path) );

	DWORD attr = GetFileAttributesA( full_path );
	if ( attr == INVALID_FILE_ATTRIBUTES || !( attr & FILE_ATTRIBUTE_DIRECTORY ) )
		return nullptr;

	const char *src = dir_name;
	while ( *src == '/' || *src == '\\' )
		++src;
	strncpy( output_dir, src, vDIRECTORY_NAME_BUF_SIZE - 1 );
	output_dir[vDIRECTORY_NAME_BUF_SIZE - 1] = 0;
	return output_dir;
}

// Skate3 code, disabled for now.
#if 0

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
bool Card::ConvertDirectory( const char* dir_name, char* output_name )
{
	// K: This used to assert.
	if (m_mounted_drive_letter==0)
	{
		return false;
	}	

	// Seems incoming filenames are of the form /foo etc.
	cardFilenameBuffer[0] = m_mounted_drive_letter;
	cardFilenameBuffer[1] = ':';
	cardFilenameBuffer[2] = '\\';
	cardFilenameBuffer[3] = 0;

	++dir_name;
	int index = 4;
	while( cardFilenameBuffer[index] = *dir_name )
	{
		// Switch forward slash directory separators to the supported backslash.
		if( cardFilenameBuffer[index] == '/' )
		{
			cardFilenameBuffer[index] = '\\';
		}
		++index;
		++dir_name;
	}

	char	output_dir[64];
	WCHAR	input_name[64];

	wsprintfW( input_name, L"%hs", &cardFilenameBuffer[4] );
	DWORD rv = XCreateSaveGame(	cardFilenameBuffer,	// Root of device on which to create the save game.
								input_name,			// Name of save game (effectively directory name).
								OPEN_EXISTING,		// Open disposition.
								0,					// Creation flags.
								output_dir,			// String to take resultant directory name buffer.
								64 );				// Size of directory name buffer.

	if( rv == ERROR_SUCCESS )
	{
		// Copy over output directory, stripping leading drive, colon and backslash, and removing trailing backslash.
		strcpy( output_name, &output_dir[3] );
		output_name[strlen( output_name ) - 1] = 0;
		return true;
	}

	return false;
}


/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
bool Card::DeleteDirectory( const char* dir_name )
{
	Dbg_Assert( m_mounted_drive_letter != 0 );

	// Seems incoming filenames are of the form /foo etc.
	cardFilenameBuffer[0] = m_mounted_drive_letter;
	cardFilenameBuffer[1] = ':';
	cardFilenameBuffer[2] = '\\';
	cardFilenameBuffer[3] = 0;

	++dir_name;
	int index = 4;
	while( cardFilenameBuffer[index] = *dir_name )
	{
		// Switch forward slash directory separators to the supported backslash.
		if( cardFilenameBuffer[index] == '/' )
		{
			cardFilenameBuffer[index] = '\\';
		}
		++index;
		++dir_name;
	}

	WCHAR	input_name[64];
	wsprintfW( input_name, L"%hs", &cardFilenameBuffer[4] );
	DWORD rv = XDeleteSaveGame(	cardFilenameBuffer,			// Root of device on which to create the save game.
								input_name );

	if( rv == ERROR_SUCCESS )
	{
		return true;
	}

	return false;
}

#endif


/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

// Given the name of a file, this will delete it's directory, which
// will result in the deletion of the directory, the file, and the icon.
// The name must not be preceded with a backslash, ie should be "Career12-Career" for example.
bool Card::DeleteDirectory( const char* dir_name )
{
	if ( dir_name == nullptr || *dir_name == 0 )
		return false;

	char full_path[MAX_FILENAME_LENGTH];
	BuildFullPath( dir_name, full_path, sizeof(full_path) );
	return RemoveTree( full_path );
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
bool Card::ChangeDirectory( const char* dir_name )
{
	(void)dir_name;
	return true;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
bool Card::Format( void )
{
	// Not supported from within an Xbox title.
	return false;
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
bool Card::Unformat( void )
{
	// Not supported from within an Xbox title.
	return false;
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
bool Card::IsFormatted( void )
{
	// Must be formatted to have got this far on an Xbox title.
	return true;
}


void Card::SetAsHardDrive()
{
	m_mounted_drive_letter='u';
}

// Skate3 code, disabled for now.
#if 0
bool Card::IsHardDrive()
{
	return m_mounted_drive_letter=='u';
}


/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void Card::UnMount( void )
{
	// Can't unmount the hard drive.
	if( m_mounted_drive_letter=='u' )
	{
		return;
	}	
	
    m_mount_error=false;
	m_mount_failed_due_to_card_full=false;
	m_mount_failed_due_to_card_unformatted=false;
	
	if( m_mounted_drive_letter )
	{
		DWORD rv = XUnmountMU(m_port, ( m_slot == 0 ) ? XDEVICE_TOP_SLOT : XDEVICE_BOTTOM_SLOT);
		if (rv != ERROR_SUCCESS)
		{
			m_mount_error=true;
			if (rv==ERROR_DISK_FULL)
			{
				m_mount_failed_due_to_card_full=true;
			}	
			if (rv==ERROR_UNRECOGNIZED_VOLUME)	
			{
				m_mount_failed_due_to_card_unformatted=true;
			}
		}    
		m_mounted_drive_letter=0;
	}
}
	
#endif


int	Card::GetDeviceType( void )
{
	return vDEV_XBOX_HARD_DRIVE;
}


// Skate3 code, disabled for now.
#if 0

bool Card::GetMountError()
{
    return m_mount_error;
}

bool Card::MountFailedDueToCardFull()
{
	return m_mount_failed_due_to_card_full;
}	

bool Card::MountFailedDueToCardUnformatted()
{
	return m_mount_failed_due_to_card_unformatted;
}	

#endif


/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
int	Card::GetNumFreeClusters( void )
{
	EnsureSaveRoot();

	ULARGE_INTEGER uli_free_avail;
	ULARGE_INTEGER uli_total;
	if ( !GetDiskFreeSpaceExA( SAVE_ROOT, &uli_free_avail, &uli_total, nullptr ) )
		return 0;

	// Report blocks as 16 KB chunks, matching the PS2/Xbox block model callers
	// expect. Cap at INT_MAX so the int return cannot wrap on modern drives.
	const ULONGLONG block_bytes = 16384ULL;
	ULONGLONG blocks = uli_free_avail.QuadPart / block_bytes;
	if ( blocks > (ULONGLONG)0x7FFFFFFF )
		blocks = 0x7FFFFFFF;
	return (int)blocks;
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
int	Card::GetNumFreeEntries( const char* path )
{
	(void)path;
	// No per-directory quota on a real filesystem. Report a large value so
	// callers that gate saves on "entries left" do not reject writes.
	return 0x7FFFFFFF;
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
bool Card::Delete( const char* filename )
{
	if ( filename == nullptr || *filename == 0 )
		return false;

	char full_path[MAX_FILENAME_LENGTH];
	BuildFullPath( filename, full_path, sizeof(full_path) );

	DWORD attr = GetFileAttributesA( full_path );
	if ( attr == INVALID_FILE_ATTRIBUTES )
		return true;	// nothing to do

	if ( attr & FILE_ATTRIBUTE_DIRECTORY )
		return RemoveTree( full_path );

	return DeleteFileA( full_path ) != 0;
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
bool Card::Rename( const char* old_name, const char* new_name )
{
	if ( old_name == nullptr || new_name == nullptr )
		return false;

	char old_full[MAX_FILENAME_LENGTH];
	char new_full[MAX_FILENAME_LENGTH];
	BuildFullPath( old_name, old_full, sizeof(old_full) );
	BuildFullPath( new_name, new_full, sizeof(new_full) );

	return MoveFileExA( old_full, new_full, MOVEFILE_REPLACE_EXISTING ) != 0;
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
File* Card::Open( const char* filename, int mode, size_t size )
{
	(void)size;

	if ( filename == nullptr || *filename == 0 )
	{
		m_last_error = vINVALID_PATH;
		return nullptr;
	}

	m_last_error = 0;
	EnsureSaveRoot();

	char full_path[MAX_FILENAME_LENGTH];
	BuildFullPath( filename, full_path, sizeof(full_path) );

	const char *stdio_mode = nullptr;
	switch ( mode )
	{
		case File::mMODE_READ:
			stdio_mode = "rb";
			break;
		case File::mMODE_WRITE:
			stdio_mode = "rb+";
			break;
		case ( File::mMODE_WRITE | File::mMODE_CREATE ):
			stdio_mode = "wb";
			break;
		case File::mMODE_CREATE:
			stdio_mode = "wbx";	// fail if exists, matches CREATE_NEW
			break;
		case ( File::mMODE_READ | File::mMODE_WRITE ):
			stdio_mode = "rb+";
			break;
		default:
			m_last_error = vACCESS_ERROR;
			return nullptr;
	}

	FILE *fp = nullptr;
	if ( fopen_s( &fp, full_path, stdio_mode ) != 0 || fp == nullptr )
	{
		if ( errno == ENOSPC )
			m_last_error = vINSUFFICIENT_SPACE;
		else
			m_last_error = vACCESS_ERROR;
		return nullptr;
	}

	File *p_file = new File( reinterpret_cast<intptr_t>( fp ), this );

	// Copy the logical filename (strip a leading slash) so callers that scan
	// the Card's file list later can match against the name they opened.
	const char *src = filename;
	while ( *src == '/' || *src == '\\' )
		++src;
	strncpy( p_file->m_Filename, src, File::vMAX_FILENAME_LEN );
	p_file->m_Filename[File::vMAX_FILENAME_LEN] = 0;

	WIN32_FILE_ATTRIBUTE_DATA attr_data;
	if ( GetFileAttributesExA( full_path, GetFileExInfoStandard, &attr_data ) )
	{
		FillDateTime( p_file->m_Created, attr_data.ftCreationTime );
		FillDateTime( p_file->m_Modified, attr_data.ftLastWriteTime );
		p_file->m_Size = attr_data.nFileSizeLow;
		p_file->m_Attribs = File::mATTRIB_READABLE | File::mATTRIB_WRITEABLE;
	}
	else
	{
		p_file->m_Size = 0;
		p_file->m_Attribs = File::mATTRIB_READABLE | File::mATTRIB_WRITEABLE;
	}

	return p_file;
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
bool Card::GetFileList( const char* mask, Lst::Head< File > &file_list )
{
	EnsureSaveRoot();

	const char *effective_mask = ( mask && *mask ) ? mask : "*";

	char search_pattern[MAX_FILENAME_LENGTH];
	_snprintf_s( search_pattern, _TRUNCATE, "%s\\%s", SAVE_ROOT, effective_mask );

	WIN32_FIND_DATAA find_data;
	HANDLE handle = FindFirstFileA( search_pattern, &find_data );
	if ( handle == INVALID_HANDLE_VALUE )
		return true;

	do
	{
		if ( strcmp( find_data.cFileName, "." ) == 0 || strcmp( find_data.cFileName, ".." ) == 0 )
			continue;

		File *new_file = new File( 0, this );

		strncpy( new_file->m_Filename, find_data.cFileName, File::vMAX_FILENAME_LEN );
		new_file->m_Filename[File::vMAX_FILENAME_LEN] = 0;

		strncpy( new_file->m_DisplayFilename, find_data.cFileName, File::vMAX_DISPLAY_FILENAME_LEN );
		new_file->m_DisplayFilename[File::vMAX_DISPLAY_FILENAME_LEN] = 0;

		FillDateTime( new_file->m_Created, find_data.ftCreationTime );
		FillDateTime( new_file->m_Modified, find_data.ftLastWriteTime );

		new_file->m_Size = find_data.nFileSizeLow;
		new_file->m_Attribs = 0;
		if ( find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			new_file->m_Attribs |= File::mATTRIB_DIRECTORY;
		if ( !( find_data.dwFileAttributes & FILE_ATTRIBUTE_READONLY ) )
			new_file->m_Attribs |= File::mATTRIB_WRITEABLE;
		new_file->m_Attribs |= File::mATTRIB_READABLE;

		file_list.AddToTail( new_file );
	}
	while ( FindNextFileA( handle, &find_data ) );
	FindClose( handle );
	return true;
}

// Xbox-era dead code kept around for reference.
#if 0
bool Card::GetFileList_Legacy( const char* mask, Lst::Head< File > &file_list )
{
	(void)mask;
	(void)file_list;

	HANDLE			handle;
	XGAME_FIND_DATA	find_data;

	cardFilenameBuffer[0] = m_mounted_drive_letter;
	cardFilenameBuffer[1] = ':';
	cardFilenameBuffer[2] = '\\';

	cardFilenameBuffer[3] = 0;
	if(( handle = XFindFirstSaveGame( cardFilenameBuffer, &find_data )) == INVALID_HANDLE_VALUE )
	{
		return true;
	}

	do
	{
		File* new_file = new File( 0, this );

		// Skip copying the drive stuff, just copy the directory, and strip the trailing '\'.
		strcpy( new_file->m_Filename, (char*)&find_data.szSaveGameDirectory[3] );
		new_file->m_Filename[strlen( new_file->m_Filename ) - 1] = 0;

		wsprintfA( new_file->m_DisplayFilename, "%ls", find_data.szSaveGameName );

		FILETIME local_file_time;
		FileTimeToLocalFileTime(&find_data.wfd.ftLastWriteTime,&local_file_time);
		SYSTEMTIME system_file_time;
		FileTimeToSystemTime(&local_file_time,&system_file_time);
		
		new_file->m_Modified.m_Year=system_file_time.wYear;
		new_file->m_Modified.m_Month=system_file_time.wMonth;
		new_file->m_Modified.m_Day=system_file_time.wDay;
		new_file->m_Modified.m_Hour=system_file_time.wHour;
		new_file->m_Modified.m_Minutes=system_file_time.wMinute;
		new_file->m_Modified.m_Seconds=system_file_time.wSecond;
		
		new_file->m_Size	= find_data.wfd.nFileSizeLow;
		new_file->m_Attribs	= 0;
		file_list.AddToTail( new_file );
	}
	while( XFindNextSaveGame( handle, &find_data ));
	XFindClose( handle );
	return true;
}
#endif


File::File( intptr_t fd, Card* card ) : Lst::Node< File > ( this ), m_fd( fd ), m_card( card )
{
}

File::~File()
{
	if ( m_fd != 0 )
	{
		fclose( reinterpret_cast<FILE *>( m_fd ) );
		m_fd = 0;
	}
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

int File::Seek( ptrdiff_t offset, FilePointerBase base )
{
	if ( m_fd == 0 )
		return -1;

	int origin;
	switch ( base )
	{
		case BASE_START:	origin = SEEK_SET; break;
		case BASE_CURRENT:	origin = SEEK_CUR; break;
		case BASE_END:		origin = SEEK_END; break;
		default:			origin = SEEK_END; break;
	}

	FILE *fp = reinterpret_cast<FILE *>( m_fd );
	if ( _fseeki64( fp, (long long)offset, origin ) != 0 )
		return -1;

	long long pos = _ftelli64( fp );
	if ( pos < 0 )
		return -1;
	if ( pos > 0x7FFFFFFF )
		pos = 0x7FFFFFFF;
	return (int)pos;
}

size_t File::Tell()
{
	if ( m_fd == 0 )
		return 0;

	long long pos = _ftelli64( reinterpret_cast<FILE *>( m_fd ) );
	if ( pos < 0 )
		return 0;
	return (size_t)pos;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

bool File::Flush( void )
{
	if ( m_fd == 0 )
		return false;

	FILE *fp = reinterpret_cast<FILE *>( m_fd );
	return fflush( fp ) == 0;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

size_t	File::Write( void* buffer, size_t len )
{
	if ( m_fd == 0 || buffer == nullptr || len == 0 )
		return 0;

	FILE *fp = reinterpret_cast<FILE *>( m_fd );
	size_t written = fwrite( buffer, 1, len, fp );
	if ( written < len && m_card != nullptr && ferror( fp ) && errno == ENOSPC )
		m_card->SetError( Card::vINSUFFICIENT_SPACE );
	return written;
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

size_t	File::Read( void* buff, size_t len )
{
	if ( m_fd == 0 || buff == nullptr || len == 0 )
		return 0;

	FILE *fp = reinterpret_cast<FILE *>( m_fd );
	return fread( buff, 1, len, fp );
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

bool File::Close( void )
{
	if ( m_fd == 0 )
		return false;

	int rc = fclose( reinterpret_cast<FILE *>( m_fd ) );
	m_fd = 0;
	return rc == 0;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

} // namespace Mc




