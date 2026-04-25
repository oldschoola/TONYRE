/*****************************************************************************
**																			**
**					   	  Neversoft Entertainment							**
**																		   	**
**				   Copyright (C) 1999 - All Rights Reserved				   	**
**																			**
******************************************************************************
**																			**
**	Project:		SYS Library												**
**																			**
**	Module:			SIO	    										        **
**																			**
**	Created:		05/26/2000	-	spg										**
**																			**
**	File name:		sys/siodev.h    										**
**																			**
*****************************************************************************/

#ifndef __SYS_SIODEV_H
#define __SYS_SIODEV_H

/*****************************************************************************
**							  	  Includes									**
*****************************************************************************/

#include <Core/List.h>
#include <Core/Task.h>
#include <Core/Support/Ref.h>

#include <cstdint>

#ifdef __PLAT_NGPS__
#include <libpad.h>
#endif

#ifdef __PLAT_XBOX__
#include <xtl.h>
#endif

/*****************************************************************************
**								   Defines									**
*****************************************************************************/

namespace SIO
{



#define CTRL_BUFFER_LENGTH      32
#define ACTUATOR_BUFFER_LENGTH  32

/*****************************************************************************
**							Class Definitions								**
*****************************************************************************/

enum Capabilities
{
    mANALOG         = 0x0001,
    mACTUATORS      = 0x0002,
    mANALOG_BUTTONS = 0x0004,
};

enum ButtonMode
{
	vDIGITAL,
	vANALOG
};

class  Data  : public Spt::Class
{
    

    friend class Device;
    friend class Search;

private :
    int                 m_type;     // controller type ( dual shock(2)/digital/analog/neGcon )
    int                 m_port;     // one of two standard ps2 controller ports
    int                 m_slot;     // (in context of multitap) which slot on that port
    bool                m_valid;    // is its control data valid?
    unsigned char*		m_dma_buff;
    unsigned char       m_control_data[CTRL_BUFFER_LENGTH];                        
    unsigned char       m_actuator_align[ACTUATOR_BUFFER_LENGTH];
    unsigned char       m_actuator_direct[ACTUATOR_BUFFER_LENGTH];
    unsigned char       m_actuator_max[ACTUATOR_BUFFER_LENGTH];
	
	// Ken: Stores the m_actuator_direct values when the game is paused.
	unsigned char       m_actuator_old_direct[ACTUATOR_BUFFER_LENGTH];
	Spt::Ref			m_paused_ref;
	
	// K: Whether the actuators are disabled.
	bool				m_actuators_disabled;
	
    Flags<Capabilities> m_caps;     // device capabilities
	int					m_button_mode;
    int                 m_num_actuators;
	void*				m_prealbuffer;
};

class  Device  : public Spt::Class
{
	

	friend class Manager;
    friend class Search;
    
public :
    
    enum Type
    {
        vNEGI_COM = 2,
        vKONAMI_GUN = 3,
        vDIGITAL_CTRL = 4,
        vJOYSTICK = 5,
        vNAMCO_GUN = 6,
        vANALOG_CTRL = 7,
        vFISHING_CTRL = 0x100,
        vJOG_CTRL = 0x300
    };


							Device( int index, int port, int slot );
	virtual					~Device();

	void			        Acquire ( void );
	void			        Unacquire ( void );
    void                    ActivateActuator( int act_num, int percent );
    void                    ActivatePressureSensitiveMode( void );
    void                    DeactivatePressureSensitiveMode( void );

    int                     GetType( void );
    int                     GetPort( void );
    int                     GetSlot( void );
    int                     GetIndex( void );
	void					SetPort( int port );
    unsigned char           *GetControlData( void );
    Flags<Capabilities>     GetCapabilities( void );
	int						GetButtonMode( void );
    bool                    HasValidControlData( void );
	bool					IsPluggedIn();
	bool					Enabled() {return !m_data.m_actuators_disabled;}


	void					Pause(); 	// K: Called when game is paused, so that the pad temporarily stops vibrating.
	void					UnPause();	// K: Called when game is unpaused.
	void					DisableActuators();	// K: Disables vibration
	void					EnableActuators();	// K: Enables vibration
	void					ResetActuators();	// Mick: Stops any vibration by disabling, then enabling (or vice versa)

							// K: Stops vibration, and ensures it won't get turned on again when
							// the game is unpaused.
	void					StopAllVibrationIncludingSaved();

	// DualSense-era extensions. On Win32 these drive the attached DualSense
	// pad (when present). On other platforms they default to no-op so PS2 /
	// Xbox builds keep compiling without platform-layer changes.
	// Hand: 0=Left trigger, 1=Right trigger, 2=Both.
	// Preset: 0=reset, 1=bow, 2=weapon, 3=machinegun, 4=galloping, 5=gamecube.
#ifdef __PLAT_WN32__
	void					SetAdaptiveTriggerPreset( int hand, int preset );
	void					SetLightbarColor( std::uint8_t r, std::uint8_t g, std::uint8_t b );
	void					SetPlayerLedIndex( int zero_based );
	bool					GetTouchpadPosition( float& x, float& y, bool& touching );
	bool					GetGyro( float& x, float& y, float& z );
#else
	void					SetAdaptiveTriggerPreset( int hand, int preset ) { (void)hand; (void)preset; }
	void					SetLightbarColor( std::uint8_t r, std::uint8_t g, std::uint8_t b ) { (void)r; (void)g; (void)b; }
	void					SetPlayerLedIndex( int zero_based ) { (void)zero_based; }
	bool					GetTouchpadPosition( float& x, float& y, bool& touching ) { (void)x; (void)y; (void)touching; return false; }
	bool					GetGyro( float& x, float& y, float& z ) { (void)x; (void)y; (void)z; return false; }
#endif

private :

    enum State
    {   
        vIDLE,
        vACQUIRING,
        vACQUIRED,
		vPRESS_MODE_ON_COMPLETE,
		vPRESS_MODE_OFF_COMPLETE,
        vBUSY,
        vNUM_STATES
    };

    enum Status
    {
        vREADY,
		vCONNECTED,
        vDISCONNECTED,
        vNOTREADY
    };
    
	Lst::Node<Device>*	    m_node;
    Data                    m_data;
    State                   m_state;
    State                   m_next_state;
    int                     m_index;

	bool					m_plugged_in; // K: True if the pad is plugged in & acquired, false otherwise.

	// DualSense / DualShock 4 dual-motor vibration state (per act_num).
	std::uint8_t			m_left_rumble = 0;
	std::uint8_t			m_right_rumble = 0;
	std::uint8_t			m_left_rumble_saved = 0;
	std::uint8_t			m_right_rumble_saved = 0;
	
    void                    process( void );
    void			        read_data ( void );         
    void                    wait( void );
    void                    acquisition_pending( void );
    void                    query_capabilities( void );
    
    int                     get_status( void );
};

/*****************************************************************************
**							 Private Declarations							**
*****************************************************************************/


/*****************************************************************************
**							  Private Prototypes							**
*****************************************************************************/


/*****************************************************************************
**							  Public Declarations							**
*****************************************************************************/


/*****************************************************************************
**							   Public Prototypes							**
*****************************************************************************/


/*****************************************************************************
**									Macros									**
*****************************************************************************/

inline  int     Device::GetType( void )
{
    

    return m_data.m_type;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

inline  int     Device::GetIndex( void )
{
    

    return m_index;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

inline  int     Device::GetPort( void )
{
    

    return m_data.m_port;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

inline	void	Device::SetPort( int port )
{

	m_data.m_port = port;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

inline  int     Device::GetSlot( void )
{
    

    return m_data.m_slot;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

inline  unsigned char    *Device::GetControlData( void )
{
    

    return m_data.m_control_data;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

inline  Flags<Capabilities>    Device::GetCapabilities( void )
{
    return m_data.m_caps;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

inline	int		Device::GetButtonMode( void )
{
	return m_data.m_button_mode;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

inline  bool    Device::HasValidControlData( void )
{
    

    return m_data.m_valid;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

} // namespace SIO

#endif	// __SYS_SIODEV_H

