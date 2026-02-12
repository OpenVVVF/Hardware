/**
  ***********************************************************************************
  * @file    vector_transfs.h
  * @author  Serhii Yatsenko [royalroad1995@gmail.com]
  * @version V1.0
  * @date    May-2020
  * @brief   This file contains the type definitions of data structures and function
  *	     prototypes for implementation the various types of Clarke and Park
  *	     transformations.
  ***********************************************************************************
  * @license
  *
  * MIT License
  *
  * Permission is hereby granted, free of charge, to any person obtaining a copy
  * of this software and associated documentation files (the "Software"), to deal
  * in the Software without restriction, including without limitation the rights
  * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  * copies of the Software, and to permit persons to whom the Software is
  * furnished to do so, subject to the following conditions:
  *
  * The above copyright notice and this permission notice shall be included in all
  * copies or substantial portions of the Software.
  *
  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  * SOFTWARE.
  *
  ***********************************************************************************
  */

/* Define to prevent recursive inclusion ------------------------------------------*/
#ifndef __VECTOR_TRANSFS_H__
#define __VECTOR_TRANSFS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes -----------------------------------------------------------------------*/
#include <math.h>

/* Exported types -----------------------------------------------------------------*/

/** 
  * @brief "Forward full Clarke transformation Module" data structure: ABC to Al-Be
  */ 
typedef struct sFFClarke
{
// Inputs:
	float fA;				// A input
	float fB;				// B input
	float fC;				// C input
// Outputs:
	float fAl;				// Alpha output
	float fBe;				// Beta output
// Functions:
	void  (*m_abc2albe)(struct sFFClarke*);	// pointer to calculation function
} tFFClarke;

/** 
  * @brief "Forward reduce Clarke transformation Module" data structure: AB to Al-Be
  */ 
typedef struct sFRClarke
{
// Inputs:
	float fA;				// A input
	float fB;				// B input
// Outputs:
	float fAl;				// Alpha output
	float fBe;				// Beta output
// Functions:
	void  (*m_ab2albe)(struct sFRClarke*);	// pointer to calculation function
} tFRClarke;

/** 
  * @brief "Inverse full Clarke transformation Module" data structure: Al-Be to ABC
  */ 
typedef struct sIFClarke
{
// Inputs:
	float fAl;				// Alpha input
	float fBe;				// Beta input
// Outputs:
	float fA;				// A output
	float fB;				// B output
	float fC;				// C output
// Functions:
	void  (*m_albe2abc)(struct sIFClarke*);	// pointer to calculation function
} tIFClarke;

/** 
  * @brief "Inverse reduce Clarke transformation Module" data structure: Al-Be to AB
  */ 
typedef struct sIRClarke
{
// Inputs:
	float fAl;				// Alpha input
	float fBe;				// Beta input
// Outputs:
	float fA;				// A output
	float fB;				// B output
// Functions:
	void  (*m_albe2ab)(struct sIRClarke*);	// pointer to calculation function
} tIRClarke;

/** 
  * @brief "Forward Park transformation Module" data structure: Al-Be to DQ
  */ 
typedef struct sFPark
{
// Inputs:
	float fAl;				// Alpha input
	float fBe;				// Beta input
	float fSinAng;				// sin(Angle) input
	float fCosAng;				// cos(Angle) input
// Outputs:
	float fD;				// D output
	float fQ;				// Q output
// Functions:
	void  (*m_albe2dq)(struct sFPark*);	// pointer to calculation function
} tFPark;

/** 
  * @brief "Inverse Park transformation Module" data structure: DQ to Al-Be
  */ 
typedef struct sIPark
{
// Inputs:
	float fD;				// D input
	float fQ;				// Q input
	float fSinAng;				// sin(Angle) input
	float fCosAng;				// cos(Angle) input
// Outputs:
	float fAl;				// Alpha output
	float fBe;				// Beta output
// Functions:
	void  (*m_dq2albe)(struct sIPark*);	// pointer to calculation function
} tIPark;

/* Exported constants -------------------------------------------------------------*/

/** 
  * @brief Initialization constant with defaults for user variables
  *	   with "tFFClarke" type
  */
#define FF_CLARKE_DEFAULTS {			\
	.fA		= 0.0f,			\
	.fB		= 0.0f,			\
	.fC		= 0.0f,			\
	.fAl		= 0.0f,			\
	.fBe		= 0.0f,			\
	.m_abc2albe	= tFFClarke_abc2albe	\
}

/** 
  * @brief Initialization constant with defaults for user variables
  *	   with "tFRClarke" type
  */
#define FR_CLARKE_DEFAULTS {			\
	.fA		= 0.0f,			\
	.fB		= 0.0f,			\
	.fAl		= 0.0f,			\
	.fBe		= 0.0f,			\
	.m_ab2albe	= tFRClarke_ab2albe	\
}

/** 
  * @brief Initialization constant with defaults for user variables
  *	   with "tIFClarke" type
  */
#define IF_CLARKE_DEFAULTS {			\
	.fAl		= 0.0f,			\
	.fBe		= 0.0f,			\
	.fA		= 0.0f,			\
	.fB		= 0.0f,			\
	.fC		= 0.0f,			\
	.m_albe2abc	= tIFClarke_albe2abc	\
}

/** 
  * @brief Initialization constant with defaults for user variables
  *	   with "tIRClarke" type
  */
#define IR_CLARKE_DEFAULTS {			\
	.fAl		= 0.0f,			\
	.fBe		= 0.0f,			\
	.fA		= 0.0f,			\
	.fB		= 0.0f,			\
	.m_albe2ab	= tIRClarke_albe2ab	\
}

/** 
  * @brief Initialization constant with defaults for user variables
  *	   with "tFPark" type
  */
#define F_PARK_DEFAULTS {			\
	.fAl		= 0.0f,			\
	.fBe		= 0.0f,			\
	.fD		= 0.0f,			\
	.fQ		= 0.0f,			\
	.fSinAng	= 0.0f,			\
	.fCosAng	= 0.0f,			\
	.m_albe2dq	= tFPark_albe2dq	\
}

/** 
  * @brief Initialization constant with defaults for user variables
  *	   with "tIPark" type
  */
#define I_PARK_DEFAULTS {			\
	.fD		= 0.0f,			\
	.fQ		= 0.0f,			\
	.fAl		= 0.0f,			\
	.fBe		= 0.0f,			\
	.fSinAng	= 0.0f,			\
	.fCosAng	= 0.0f,			\
	.m_dq2albe	= tIPark_dq2albe	\
}
	
/* Exported macro -----------------------------------------------------------------*/
/* Exported functions -------------------------------------------------------------*/

/* Forward full Clarke transformation function prototype ***************************/
void tFFClarke_abc2albe(tFFClarke*);

/* Forward reduce Clarke transformation function prototype *************************/
void tFRClarke_ab2albe(tFRClarke*);

/* Inverse full Clarke transformation function prototype ***************************/
void tIFClarke_albe2abc(tIFClarke*);

/* Inverse reduce Clarke transformation function prototype *************************/
void tIRClarke_albe2ab(tIRClarke*);

/* Forward Park transformation function prototype **********************************/
void tFPark_albe2dq(tFPark*);

/* Inverse Park transformation function prototype **********************************/
void tIPark_dq2albe(tIPark*);

#ifdef __cplusplus
}
#endif

#endif /* __VECTOR_TRANSFS_H__ */

/*********************************** END OF FILE ***********************************/
