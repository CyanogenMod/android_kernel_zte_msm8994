//********************************************************************************
//
//		<< LC898122 Evaluation Soft>>
//		Program Name	: Ois.h
// 		Explanation		: LC898122 Global Declaration & ProtType Declaration
//		Design			: Y.Yamada
//		History			: First edition						2009.07.30 Y.Tashita
//********************************************************************************
#include "msm_ois_lc898122_sharp.h"
#define	FW_VER			0x0007     //Countermeasure-2
#ifdef	OISINI
	#define	OISINI__
#else
	#define	OISINI__		extern
#endif
#if 1
extern void RegReadA(unsigned short reg_addr, unsigned char *read_data_8);
extern void RegWriteA(unsigned short reg_addr, unsigned char write_data_8);
extern void RamReadA(unsigned short ram_addr, void *read_data_16);
extern void RamWriteA(unsigned short ram_addr, unsigned short write_data_16);
extern void RamRead32A(unsigned short ram_addr, void *read_data_32);
extern void RamWrite32A(unsigned short ram_addr, uint32_t write_data_32);
#endif




#ifdef	OISCMD
	#define	OISCMD__
#else
	#define	OISCMD__		extern
#endif


// Define According To Usage

/****************************** Define���� ******************************/
/*	USE_3WIRE_DGYRO		Digital Gyro I/F 3��Mode�g�p					*/
/*	USE_PANASONIC		Pansonic Digital Gyro�g�p						*/
/*	USE_INVENSENSE		Invensense Digital Gyro�g�p						*/
/*		USE_IDG2020		Inv IDG-2020�g�p								*/
/*	USE_STMICRO_L3G4IS	ST-Micro Digital Gyro L3G4IS�g�p				*/
/*	STANDBY_MODE		Standby����g�p(���m�F)							*/
/*	GAIN_CONT			:Gain control�@�\�g�p							*/
/*		(disable)		DSC			:�O�rMode�g�p						*/
/*	HALLADJ_HW			Hall Calibration LSI�@�\�g�p					*/
/*	NEUTRAL_CENTER		Servo Center��������p���̓d��0mA�Ƃ���			*/
/*	INIT_PWMMODE		PWM���[�h�I�� PWM or CVL-PWM					*/
/*							PWMMOD_PWM ->     PWM MODE�g�p				*/
/*							PWMMOD_CVL -> CVL-PWM MODE�g�p				*/
/*	ACTREG_6P5OHM		6.5���A�N�`���G�[�^�g�p							*/
/*	DEF_SET				LSI�����ݒ�f�t�H���g�l�g�p						*/
/*	SXQ_INI, SYQ_INI	Hall Filter�ɐ��ݒ�								*/
/*	GXHY_GYHX			Gyro Filter -> Hall Filter�ڑ��ݒ�				*/
/************************************************************************/

/* Select Gyro Sensor */
//#define		USE_3WIRE_DGYRO		//for D-Gyro 3-Wire SPI interface

//#define		USE_PANASONIC		// Panasonic or Other
//#define		USE_STMICRO_L3G4IS	// ST-Micro-L3G4IS
#define		USE_INVENSENSE		// INVENSENSE

#ifdef USE_INVENSENSE
//	#define		FS_SEL		0		/* �}262LSB/��/s  */
//	#define		FS_SEL		1		/* �}131LSB/��/s  */
//	#define		FS_SEL		2		/* �}65.5LSB/��/s  */
	#define		FS_SEL		3		/* �}32.8LSB/��/s  */
	
//	#define		GYROSTBY			/* Sleep+STBY */
	#define		GYROX_INI		0x45	// Gyro X axis select
	#define		GYROY_INI		0x43	// Gyro Y axis select
#endif

#ifdef	USE_STMICRO_L3G4IS
	#define		GYROX_INI		0x43	// Gyro X axis select
	#define		GYROY_INI		0x45	// Gyro Y axis select
#endif

#ifdef USE_PANASONIC
	#define		GYROX_INI		0x7C	// Gyro X axis select
	#define		GYROY_INI		0x78	// Gyro Y axis select
#endif


/* Select Mode */
#define		STANDBY_MODE			// STANDBY Mode
#define		GAIN_CONT				// Gain Control Mode

//#define	ACTREG_6P5OHM			// Use 6.5ohm Actuator
#define		ACTREG_10P5OHM			// Use 10.5ohm
//#define	ACTREG_15OHM			// Use 15ohm
#define		PWM_BREAK				// PWM mode select (disable zero cross)
//#define		AF_PWMMODE			// AF Driver PWM mode

#define		DEF_SET				// default value re-setting
//#define		USE_EXTCLK_ALL		// USE Ext clk for ALL
//#define		USE_EXTCLK_PWM		// USE Ext clk for PWM
//#define		USE_VH_SYNC			// USE V/H Sync for PWM
//#define		PWM_CAREER_TEST		// PWM_CAREER_TEST
#define		MONITOR_OFF			// default Monitor output

//#define		HALLADJ_HW			// H/W Hall adjustment 
#define		NEUTRAL_CENTER			// Upper Position Current 0mA Measurement
//#define		MODULE_CALIBRATION		// for module maker   use float

#ifdef	MODULE_CALIBRATION
// #define	OSC_I2CCK				// Adj by I2C Clk
// #define	OSC_EXCLK				// Adj by Ex-Clk
#endif

#define		H1COEF_CHANGER			/* H1 coef lvl chage */
#define		CORRECT_04DEG			// Correct pm0.42deg
//#define		CORRECT_05DEG			// Correct pm0.5deg
//define		CORRECT_07DEG			// Correct pm0.7deg
//#define		CORRECT_12DEG				// Correct pm1.2deg
//#define		PT_TEST_01

//#define		NEW_PTST				// method of Pan/Tilt

/* OIS Calibration Parameter */

 #define		DAHLXO_INI		0x0000		// Hall X Offset
 #define		DAHLXB_INI		0xE000		// Hall X Bias
 #define		DAHLYO_INI		0x0000		// Hall Y Offset
 #define		DAHLYB_INI		0xE000		// Hall X Bias
 #define		HXOFF0Z_INI		0x0000		// Hall X AD Offset
 #define		HYOFF1Z_INI		0x0000		// Hall Y AD Offset
// #define		OPTCEN_X		0x0000		// Hall X Optical Offset
// #define		OPTCEN_Y		0x0000		// Hall Y Optical Offset
 #define		SXGAIN_INI		0x3000		// Hall X Loop Gain
 #define		SYGAIN_INI		0x3000		// Hall Y Loop Gain
 #define		DGYRO_OFST_XH	0x00		// Digital Gyro X AD Offset H
 #define		DGYRO_OFST_XL	0x00		// Digital Gyro X AD Offset L
 #define		DGYRO_OFST_YH	0x00		// Digital Gyro Y AD Offset H
 #define		DGYRO_OFST_YL	0x00		// Digital Gyro Y AD Offset L
 #define		GXGAIN_INI		0x3F800000	// Gyro X Zoom Value
 #define		GYGAIN_INI		0x3F800000	// Gyro Y Zoom Value
 #define		OSC_INI			0x2E		/* OSC Init, VDD=2.8V */


/* Actuator Select */
 #define		BIAS_CUR_OIS	0x33		// 2.0mA/2.0mA
 #define		AMP_GAIN_X		0x04		// x100
 #define		AMP_GAIN_Y		0x04		// x100
 
/* AF Open parameter */
 #define		RWEXD1_L_AF		0x7FFF		//
 #define		RWEXD2_L_AF		0x39F0		//
 #define		RWEXD3_L_AF		0x638D		//
 #define		FSTCTIME_AF		0xF1		//
 #define		FSTMODE_AF		0x00		//

/* AF adjust parameter */
#define		DAHLZB_INI		0x9000
#define		DAHLZO_INI		0x0000
#define		BIAS_CUR_AF		0x00			//0.25mA
#define		AMP_GAIN_AF		0x00			//x6


/*** Hall, Gyro Parameter Setting ***/
/* Hall Parameter */
 #define		SXGAIN_LOP		0x3000		// X Loop Gain Adjust Sin Wave Amplitude
 #define		SYGAIN_LOP		0x3000		// Y Loop Gain Adjust Sin Wave Amplitude
 
 #define		SXQ_INI			0xBF800000	/* Hall Filter Connection Setting(sxq, syq) */
 #define		SYQ_INI			0x3F800000		// 0x3F800000 -> Positive
												// 0xBF800000 -> Negative
/* Gyro Parameter */
#define		GXHY_GYHX		0				/* Gyro Filter Connection Setting */
												// 0 : GyroX -> HallX, GyroY -> HallY 
												// 1 : GyroX -> HallY, GyroY -> HallX 

#define		TCODEH_ADJ		0x0000

#ifdef	CORRECT_04DEG
 #define		GYRLMT1H		0x3DCCCCC0		//0.1F
 #define		GYRLMT3_S1		0x3EAE147B		//0.34F
 #define		GYRLMT3_S2		0x3EAE147B		//0.34F
 
 #define		GYRLMT4_S1		0x3FD9999A	//1.7F
 #define		GYRLMT4_S2		0x3FD9999A	//1.7F

 #define		GYRA12_HGH		0x3FBE6666	/* 1.488F */
 #define		GYRA12_MID		0x3F59999A	/* 0.85F */
 #define		GYRA34_HGH		0x3F000000	/* 0.5F */
 #define		GYRA34_MID		0x3DCCCCCD	/* 0.1F */

 #define		GYRB12_HGH		0x3DCCCCCD	/* 0.10F */
 #define		GYRB12_MID		0x3CA3D70A	/* 0.02F */
 #define		GYRB34_HGH		0x3CA3D70A	/* 0.02F */
 #define		GYRB34_MID		0x3C23D70A	/* 0.01F */
#endif	//CORRECT_04DEG

#ifdef	CORRECT_05DEG
 #define		GYRLMT1H		0x3DCCCCC0		//0.1F
 #define		GYRLMT3_S1		0x3ECCCCCD		//0.40F
 #define		GYRLMT3_S2		0x3ECCCCCD		//0.40F
 
 #define		GYRLMT4_S1		0x40000000	//2.0F
 #define		GYRLMT4_S2		0x40000000	//2.0F

 #define		GYRA12_HGH		0x3FE00000	/* 1.75F */
 #define		GYRA12_MID		0x3F800000	/* 1.0F */
 #define		GYRA34_HGH		0x3F000000	/* 0.5F */
 #define		GYRA34_MID		0x3DCCCCCD	/* 0.1F */

 #define		GYRB12_HGH		0x3DCCCCCD	/* 0.10F */
 #define		GYRB12_MID		0x3CA3D70A	/* 0.02F */
 #define		GYRB34_HGH		0x3CA3D70A	/* 0.02F */
 #define		GYRB34_MID		0x3C23D70A	/* 0.01F */
#endif	//CORRECT_05DEG

#ifdef	CORRECT_07DEG
 #define		GYRLMT1H		0x3DCCCCC0		//0.1F
 #define		GYRLMT3_S1		0x3F0F5C29		//0.56F
 #define		GYRLMT3_S2		0x3F0F5C29		//0.56F

 #define		GYRLMT4_S1		0x40333333	//2.8F
 #define		GYRLMT4_S2		0x40333333	//2.8F

 #define		GYRA12_HGH		0x401CCCCD	/* 2.45F */
 #define		GYRA12_MID		0x3FB33333	/* 1.4F */
 #define		GYRA34_HGH		0x3F000000	/* 0.5F */
 #define		GYRA34_MID		0x3DCCCCCD	/* 0.1F */

 #define		GYRB12_HGH		0x3E4CCCCD	/* 0.20F */
 #define		GYRB12_MID		0x3CA3D70A	/* 0.02F */
 #define		GYRB34_HGH		0x3CA3D70A	/* 0.02F */
 #define		GYRB34_MID		0x3C23D70A	/* 0.01F */
#endif	//CORRECT_07DEG

#ifdef	CORRECT_12DEG
 #define		GYRLMT1H		0x3DCCCCC0		//0.1F
 #define		GYRLMT3_S1		0x3F2F8AF9		//0.686F
 #define		GYRLMT3_S2		0x3F2F8AF9		//0.686F

 #define		GYRLMT4_S1		0x405B6DB7	//3.429F
 #define		GYRLMT4_S2		0x405B6DB7	//3.429F

 #define		GYRA12_HGH		0x40400000	/* 3.000F */
 #define		GYRA12_MID		0x3FDB6DB7	/* 1.714F */
 #define		GYRA34_HGH		0x3F000000	/* 0.5F */
 #define		GYRA34_MID		0x3DCCCCCD	/* 0.1F */

 #define		GYRB12_HGH		0x3E4CCCCD	/* 0.20F */
 #define		GYRB12_MID		0x3CA3D70A	/* 0.02F */
 #define		GYRB34_HGH		0x3CA3D70A	/* 0.02F */
 #define		GYRB34_MID		0x3C23D70A	/* 0.01F */
#endif	//CORRECT_12DEG

// Command Status
#define		EXE_END		0x02		// Execute End (Adjust OK)
#define		EXE_HXADJ	0x06		// Adjust NG : X Hall NG (Gain or Offset)
#define		EXE_HYADJ	0x0A		// Adjust NG : Y Hall NG (Gain or Offset)
#define		EXE_LXADJ	0x12		// Adjust NG : X Loop NG (Gain)
#define		EXE_LYADJ	0x22		// Adjust NG : Y Loop NG (Gain)
#define		EXE_GXADJ	0x42		// Adjust NG : X Gyro NG (offset)
#define		EXE_GYADJ	0x82		// Adjust NG : Y Gyro NG (offset)
#define		EXE_OCADJ	0x402		// Adjust NG : OSC Clock NG
#define		EXE_ERR		0x99		// Execute Error End

// Common Define
#define	SUCCESS			0x00		// Success
#define	FAILURE			0x01		// Failure

#ifndef ON
 #define	ON			0x01		// ON
 #define	OFF			0x00		// OFF
#endif
 #define	SPC			0x02		// Special Mode

#define	X_DIR			0x00		// X Direction
#define	Y_DIR			0x01		// Y Direction
#define	X2_DIR			0x10		// X Direction
#define	Y2_DIR			0x11		// Y Direction

#define	NOP_TIME		0.00004166F

#ifdef STANDBY_MODE
 // Standby mode
 #define		STB1_ON		0x00		// Standby1 ON
 #define		STB1_OFF	0x01		// Standby1 OFF
 #define		STB2_ON		0x02		// Standby2 ON
 #define		STB2_OFF	0x03		// Standby2 OFF
 #define		STB3_ON		0x04		// Standby3 ON
 #define		STB3_OFF	0x05		// Standby3 OFF
 #define		STB4_ON		0x06		// Standby4 ON			/* for Digital Gyro Read */
 #define		STB4_OFF	0x07		// Standby4 OFF
 #define		STB2_OISON	0x08		// Standby2 ON (only OIS)
 #define		STB2_OISOFF	0x09		// Standby2 OFF(only OIS)
 #define		STB2_AFON	0x0A		// Standby2 ON (only AF)
 #define		STB2_AFOFF	0x0B		// Standby2 OFF(only AF)
#endif

/* Optical Center & Gyro Gain for Mode */
 #define	VAL_SET				0x00		// Setting mode
 #define	VAL_FIX				0x01		// Fix Set value
 #define	VAL_SPC				0x02		// Special mode


struct STFILREG {
	unsigned short	UsRegAdd ;
	unsigned char	UcRegDat ;
} ;													// Register Data Table

struct STFILRAM {
	unsigned short	UsRamAdd ;
	uint32_t	UlRamDat ;
} ;													// Filter Coefficient Table

struct STCMDTBL
{
	unsigned short Cmd ;
	unsigned int UiCmdStf ;
	void ( *UcCmdPtr )( void ) ;
} ;

/*** caution [little-endian] ***/

// Word Data Union
union	WRDVAL{
	unsigned short	UsWrdVal ;
	unsigned char	UcWrkVal[ 2 ] ;
	struct {
		unsigned char	UcLowVal ;
		unsigned char	UcHigVal ;
	} StWrdVal ;
} ;

typedef union WRDVAL	UnWrdVal ;

union	DWDVAL {
	uint32_t	UlDwdVal ;
	unsigned short	UsDwdVal[ 2 ] ;
	struct {
		unsigned short	UsLowVal ;
		unsigned short	UsHigVal ;
	} StDwdVal ;
	struct {
		unsigned char	UcRamVa0 ;
		unsigned char	UcRamVa1 ;
		unsigned char	UcRamVa2 ;
		unsigned char	UcRamVa3 ;
	} StCdwVal ;
} ;

typedef union DWDVAL	UnDwdVal;

// Float Data Union
union	FLTVAL {
	float			SfFltVal ;
	uint32_t	UlLngVal ;
	unsigned short	UsDwdVal[ 2 ] ;
	struct {
		unsigned short	UsLowVal ;
		unsigned short	UsHigVal ;
	} StFltVal ;
} ;

typedef union FLTVAL	UnFltVal ;


typedef struct STADJPAR {
	struct {
		unsigned char	UcAdjPhs ;				// Hall Adjust Phase

		unsigned short	UsHlxCna ;				// Hall Center Value after Hall Adjust
		unsigned short	UsHlxMax ;				// Hall Max Value
		unsigned short	UsHlxMxa ;				// Hall Max Value after Hall Adjust
		unsigned short	UsHlxMin ;				// Hall Min Value
		unsigned short	UsHlxMna ;				// Hall Min Value after Hall Adjust
		unsigned short	UsHlxGan ;				// Hall Gain Value
		unsigned short	UsHlxOff ;				// Hall Offset Value
		unsigned short	UsAdxOff ;				// Hall A/D Offset Value
		unsigned short	UsHlxCen ;				// Hall Center Value

		unsigned short	UsHlyCna ;				// Hall Center Value after Hall Adjust
		unsigned short	UsHlyMax ;				// Hall Max Value
		unsigned short	UsHlyMxa ;				// Hall Max Value after Hall Adjust
		unsigned short	UsHlyMin ;				// Hall Min Value
		unsigned short	UsHlyMna ;				// Hall Min Value after Hall Adjust
		unsigned short	UsHlyGan ;				// Hall Gain Value
		unsigned short	UsHlyOff ;				// Hall Offset Value
		unsigned short	UsAdyOff ;				// Hall A/D Offset Value
		unsigned short	UsHlyCen ;				// Hall Center Value
	} StHalAdj ;

	struct {
		unsigned short	UsLxgVal ;				// Loop Gain X
		unsigned short	UsLygVal ;				// Loop Gain Y
		unsigned short	UsLxgSts ;				// Loop Gain X Status
		unsigned short	UsLygSts ;				// Loop Gain Y Status
	} StLopGan ;

	struct {
		unsigned short	UsGxoVal ;				// Gyro A/D Offset X
		unsigned short	UsGyoVal ;				// Gyro A/D Offset Y
		unsigned short	UsGxoSts ;				// Gyro Offset X Status
		unsigned short	UsGyoSts ;				// Gyro Offset Y Status
	} StGvcOff ;
	
	unsigned char		UcOscVal ;				// OSC value

} stAdjPar ;

OISCMD__	stAdjPar	StAdjPar ;				// Execute Command Parameter

OISCMD__	unsigned char	UcOscAdjFlg ;		// For Measure trigger
  #define	MEASSTR		0x01
  #define	MEASCNT		0x08
  #define	MEASFIX		0x80

OISINI__	unsigned short	UsCntXof ;			/* OPTICAL Center Xvalue */
OISINI__	unsigned short	UsCntYof ;			/* OPTICAL Center Yvalue */

OISINI__	unsigned char	UcPwmMod ;			/* PWM MODE */
#define		PWMMOD_CVL	0x00						// CVL PWM MODE
#define		PWMMOD_PWM	0x01						// PWM MODE

#define		INIT_PWMMODE	PWMMOD_CVL			/* PWM/CVL MODE select */
													// PWMMOD_PWM ->     PWM MODE
													// PWMMOD_CVL -> CVL-PWM MODE


OISINI__	unsigned char	UcCvrCod ;			/* CverCode */
 #define	CVER122		0x93					 // LC898122



// Prottype Declation
OISINI__ void	IniSet( void ) ;													// Initial Top Function
OISINI__ void	IniSetAf( void ) ;													// Initial Top Function

OISINI__ void	ClrGyr( unsigned short, unsigned char ); 							   // Clear Gyro RAM
	#define CLR_FRAM0		 	0x01
	#define CLR_FRAM1 			0x02
	#define CLR_ALL_RAM 		0x03
OISINI__ void	BsyWit( unsigned short, unsigned char ) ;				// Busy Wait Function
OISINI__ void	WitTim( unsigned short ) ;								// Wait
	 #define	WTLE 			10										// Waiting Time For Loop Escape
OISINI__ void	MemClr( unsigned char *, unsigned short ) ;				// Memory Clear Function
OISINI__ void	GyOutSignal( void ) ;									// Slect Gyro Output signal Function
OISINI__ void	GyOutSignalCont( void ) ;								// Slect Gyro Output Continuos Function
#ifdef STANDBY_MODE
OISINI__ void	AccWit( unsigned char ) ;								// Acc Wait Function
OISINI__ void	SelectGySleep( unsigned char ) ;						// Select Gyro Mode Function
#endif
#ifdef	GAIN_CONT
OISINI__ void	AutoGainControlSw( unsigned char ) ;					// Auto Gain Control Sw
#endif
OISINI__ void	DrvSw( unsigned char UcDrvSw ) ;						// Driver Mode setting function
OISINI__ void	AfDrvSw( unsigned char UcDrvSw ) ;						// AF Driver Mode setting function
OISINI__ void	RamAccFixMod( unsigned char ) ;							// Ram Access Fix Mode setting function
OISINI__ void	IniPtMovMod( unsigned char ) ;							// Pan/Tilt parameter setting by mode function
OISINI__ void	ChkCvr( void ) ;										// Check Function
	
OISCMD__ void			SrvCon( unsigned char, unsigned char ) ;					// Servo ON/OFF
OISCMD__ unsigned short	TneRun( void ) ;											// Hall System Auto Adjustment Function
OISCMD__ unsigned char	RtnCen( unsigned char ) ;									// Return to Center Function
OISCMD__ void			OisEna( void ) ;											// OIS Enable Function
OISCMD__ void			OisEnaLin( void ) ;											// OIS Enable Function for Line adjustment
OISCMD__ void			TimPro( void ) ;											// Timer Interrupt Process Function
OISCMD__ void			S2cPro( unsigned char ) ;									// S2 Command Process Function
	#define		DIFIL_S2		0x3F7FFE00
OISCMD__ void			SetSinWavePara( unsigned char , unsigned char ) ;			// Sin wave Test Function
	#define		SINEWAVE	0
	#define		XHALWAVE	1
	#define		YHALWAVE	2
	#define		CIRCWAVE	255
OISCMD__ unsigned char	TneGvc( void ) ;											// Gyro VC Offset Adjust

OISCMD__ void			SetZsp( unsigned char ) ;									// Set Zoom Step parameter Function
OISCMD__ void			OptCen( unsigned char, unsigned short, unsigned short ) ;	// Set Optical Center adjusted value Function
OISCMD__ void			StbOnnN( unsigned char , unsigned char ) ;					// Stabilizer For Servo On Function
#ifdef	MODULE_CALIBRATION
OISCMD__ unsigned char	LopGan( unsigned char ) ;									// Loop Gain Adjust
#endif
#ifdef STANDBY_MODE
 OISCMD__ void			SetStandby( unsigned char ) ;								/* Standby control	*/
#endif
#ifdef	MODULE_CALIBRATION
OISCMD__ unsigned short	OscAdj( void ) ;											/* OSC clock adjustment */
OISCMD__ unsigned short	OscAdjA( unsigned short ) ;									/* OSC clock adjustment Semi Auto */
#endif

#ifdef	HALLADJ_HW
 #ifdef	MODULE_CALIBRATION
 OISCMD__ unsigned char	LoopGainAdj(   unsigned char );
 #endif
 OISCMD__ unsigned char	BiasOffsetAdj( unsigned char , unsigned char );
#endif
OISCMD__ void			GyrGan( unsigned char , uint32_t , uint32_t ) ;	/* Set Gyro Gain Function */
OISCMD__ void			SetPanTiltMode( unsigned char ) ;							/* Pan_Tilt control Function */
#ifndef	HALLADJ_HW
 OISCMD__ uint32_t	TnePtp( unsigned char, unsigned char ) ;					// Get Hall Peak to Peak Values
 #define		HALL_H_VAL	0x3F800000												/* 1.0 */

 OISCMD__ unsigned char	TneCen( unsigned char, UnDwdVal ) ;							// Tuning Hall Center
 #define		PTP_BEFORE		0
 #define		PTP_AFTER		1
#endif
#ifdef GAIN_CONT
OISCMD__ unsigned char	TriSts( void ) ;											// Read Status of Tripod mode Function
#endif
OISCMD__ unsigned char	DrvPwmSw( unsigned char ) ;									// Select Driver mode Function
	#define		Mlnp		0					// Linear PWM
	#define		Mpwm		1					// PWM
 #ifdef	NEUTRAL_CENTER																// Gyro VC Offset Adjust
 OISCMD__ unsigned char	TneHvc( void ) ;											// Hall VC Offset Adjust
 #endif	//NEUTRAL_CENTER
OISCMD__ void			SetGcf( unsigned char ) ;									// Set DI filter coefficient Function
OISCMD__	uint32_t	UlH1Coefval ;		// H1 coefficient value
#ifdef H1COEF_CHANGER
 OISCMD__	unsigned char	UcH1LvlMod ;		// H1 level coef mode
 OISCMD__	void			SetH1cMod( unsigned char ) ;							// Set H1C coefficient Level chang Function
 #define		S2MODE		0x40
 #define		ACTMODE		0x80
 #define		MOVMODE		0xFF
#endif
OISCMD__	unsigned short	RdFwVr( void ) ;										// Read Fw Version Function
