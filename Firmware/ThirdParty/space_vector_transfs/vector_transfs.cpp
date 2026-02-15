/**
  ***********************************************************************************
  * @file    vector_transfs.c
  * @author  Serhii Yatsenko [royalroad1995@gmail.com]
  * @version V1.0
  * @date    May-2020
  * @brief   This file provides firmware functions for implementation the following
  *	     types of Clarke and Park transformations:
  *		+ Forward full Clarke transformation - ABC to Alpha-Beta coordinates
  *           	+ Forward reduce Clarke transformation - AB to Alpha-Beta coordinates
  *		  in case when A + B + C = 0
  *           	+ Inverse full Clarke transformation - Alpha-Beta to ABC coordinates
  *           	+ Inverse reduce Clarke transformation - Alpha-Beta to AB coordinates
  *		  in case when A + B + C = 0
  *           	+ Forward Park transformation - Alpha-Beta to DQ coordinates
  *           	+ Inverse Park transformation - DQ to Alpha-Beta coordinates
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

/* Includes -----------------------------------------------------------------------*/
#include "vector_transfs.h"

/* Private typedef ----------------------------------------------------------------*/
/* Private define -----------------------------------------------------------------*/
#define SQRT_3 sqrtf(3.0f)

/* Private constants --------------------------------------------------------------*/
/* Private macro ------------------------------------------------------------------*/
/* Private variables --------------------------------------------------------------*/
/* Private function prototypes ----------------------------------------------------*/
/* Private functions --------------------------------------------------------------*/

/**
  * @brief  Forward full Clarke transformation function: ABC to Al-Be.
  * @param  ptFFClarke: pointer to user data structure with type "tFFClarke".               
  * @retval None
  */
void tFFClarke_abc2albe(tFFClarke* ptFFClarke)
{
	ptFFClarke->fAl = (2.0f/3.0f)*ptFFClarke->fA - 
			  (1.0f/3.0f)*(ptFFClarke->fB - ptFFClarke->fC);
	ptFFClarke->fBe = (2.0f/SQRT_3)*(ptFFClarke->fB - ptFFClarke->fC);
}

/**
  * @brief  Forward reduce Clarke transformation function: AB to Al-Be.
  * @param  ptFRClarke: pointer to user data structure with type "tFRClarke".
  * @retval None
  */
void tFRClarke_ab2albe(tFRClarke* ptFRClarke)
{
	ptFRClarke->fAl = ptFRClarke->fA;
	ptFRClarke->fBe = (1.0f/SQRT_3)*(ptFRClarke->fA + 2.0f*ptFRClarke->fB);
}

/**
  * @brief  Inverse full Clarke transformation function: Al-Be to ABC.
  * @param  ptIFClarke: pointer to user data structure with type "tIFClarke".
  * @retval None
  */
void tIFClarke_albe2abc(tIFClarke* ptIFClarke)
{
	 ptIFClarke->fA = ptIFClarke->fAl;
	 ptIFClarke->fB = 0.5f*(-ptIFClarke->fAl + SQRT_3*ptIFClarke->fBe);
	 ptIFClarke->fC = 0.5f*(-ptIFClarke->fAl - SQRT_3*ptIFClarke->fBe);
}

/**
  * @brief  Inverse reduce Clarke transformation function: Al-Be to AB.
  * @param  ptIRClarke: pointer to user data structure with type "tIRClarke".
  * @retval None
  */
void tIRClarke_albe2ab(tIRClarke* ptIRClarke)
{
	ptIRClarke->fA = ptIRClarke->fAl;
	ptIRClarke->fB = 0.5f*(SQRT_3*ptIRClarke->fBe - ptIRClarke->fAl);
}

/**
  * @brief  Forward Park transformation function: Al-Be to DQ.
  * @param  ptFPark: pointer to user data structure with type "tFPark".
  * @retval None
  */
void tFPark_albe2dq(tFPark* ptFPark)
{
	ptFPark->fD = ptFPark->fAl*ptFPark->fCosAng + ptFPark->fBe*ptFPark->fSinAng;
	ptFPark->fQ = ptFPark->fBe*ptFPark->fCosAng - ptFPark->fAl*ptFPark->fSinAng;
}

/**
  * @brief  Inverse Park transformation function: DQ to Al-Be.
  * @param  ptIPark: pointer to user data structure with type "tIPark".
  * @retval None
  */
void tIPark_dq2albe(tIPark* ptIPark)
{
	ptIPark->fAl = ptIPark->fD*ptIPark->fCosAng - ptIPark->fQ*ptIPark->fSinAng;
	ptIPark->fBe = ptIPark->fQ*ptIPark->fCosAng + ptIPark->fD*ptIPark->fSinAng;
}

/*********************************** END OF FILE ***********************************/
