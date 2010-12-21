/******************************************************************************
 *
 * Project:  integrating laszip into liblas - http://liblas.org -
 * Purpose:
 * Author:   Martin Isenburg
 *           isenburg at cs.unc.edu
 *
 ******************************************************************************
 * Copyright (c) 2010, Martin Isenburg
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of the GNU Lesser General Licence as published
 * by the Free Software Foundation.
 *
 * See the COPYING file for more information.
 *
 ****************************************************************************/

/*
===============================================================================

  FILE:  lasreaditemcompressed_v1.cpp
  
  CONTENTS:
  
    see corresponding header file
  
  PROGRAMMERS:
  
    martin isenburg@cs.unc.edu
  
  COPYRIGHT:
  
    copyright (C) 2010  martin isenburg@cs.unc.edu
    
    This software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  
  CHANGE HISTORY:
  
    see corresponding header file
  
===============================================================================
*/

#include "lasreaditemcompressed_v1.hpp"

#include <assert.h>
#include <string.h>

/*
===============================================================================
                       LASreadItemCompressed_POINT10_v1
===============================================================================
*/

struct LASpoint10
{
  int x;
  int y;
  int z;
  unsigned short intensity;
  unsigned char return_number : 3;
  unsigned char number_of_returns_of_given_pulse : 3;
  unsigned char scan_direction_flag : 1;
  unsigned char edge_of_flight_line : 1;
  unsigned char classification;
  char scan_angle_rank;
  unsigned char user_data;
  unsigned short point_source_ID;
};

LASreadItemCompressed_POINT10_v1::LASreadItemCompressed_POINT10_v1(EntropyDecoder* dec)
{
  /* set decoder */
  assert(dec);
  this->dec = dec;

  /* create models and integer compressors */
  ic_dx = new IntegerCompressor(dec, 32, 2);  // 32 bits, 2 contexts
	ic_dy = new IntegerCompressor(dec, 32, 33); // 32 bits, 33 contexts
	ic_z = new IntegerCompressor(dec, 32, 33);  // 32 bits, 33 contexts
	m_changed_values = dec->createSymbolModel(64);
	ic_intensity = new IntegerCompressor(dec, 16);
	m_bit_byte = dec->createSymbolModel(256);
  m_classification = dec->createSymbolModel(256);
	ic_scan_angle_rank = new IntegerCompressor(dec, 8, 2);
	m_user_data = dec->createSymbolModel(256);
	ic_point_source_ID = new IntegerCompressor(dec, 16);

  /* create last item */
  last_item = new U8[20];
}

LASreadItemCompressed_POINT10_v1::~LASreadItemCompressed_POINT10_v1()
{
  delete ic_dx;
  delete ic_dy;
  delete ic_z;
  dec->destroySymbolModel(m_changed_values);
  delete ic_intensity;
  dec->destroySymbolModel(m_bit_byte);
  dec->destroySymbolModel(m_classification);
  delete ic_scan_angle_rank;
  dec->destroySymbolModel(m_user_data);
  delete ic_point_source_ID;
  delete [] last_item;
}


BOOL LASreadItemCompressed_POINT10_v1::init(const U8* item)
{
  /* init state */
  last_dir = 0;
	last_x_diff[0][0] = last_x_diff[0][1] = last_x_diff[0][2] = last_x_diff[1][0] = last_x_diff[1][1] = last_x_diff[1][2] = 0;
	last_y_diff[0][0] = last_y_diff[0][1] = last_y_diff[0][2] = last_y_diff[1][0] = last_y_diff[1][1] = last_y_diff[1][2] = 0;
  last_incr[0] = last_incr[1] = 0;

  /* init models and integer compressors */
  ic_dx->initDecompressor();
  ic_dy->initDecompressor();
  ic_z->initDecompressor();
  dec->initSymbolModel(m_changed_values);
  ic_intensity->initDecompressor();
  dec->initSymbolModel(m_bit_byte);
  dec->initSymbolModel(m_classification);
  ic_scan_angle_rank->initDecompressor();
  dec->initSymbolModel(m_user_data);
  ic_point_source_ID->initDecompressor();

  /* init last item */
  memcpy(last_item, item, 20);

  return TRUE;
}

inline BOOL LASreadItemCompressed_POINT10_v1::read(U8* item)
{
  // find median difference for x and y from 3 preceding differences
  I32 median_x;
  if (last_x_diff[last_dir][0] < last_x_diff[last_dir][1])
  {
    if (last_x_diff[last_dir][1] < last_x_diff[last_dir][2])
      median_x = last_x_diff[last_dir][1];
    else if (last_x_diff[last_dir][0] < last_x_diff[last_dir][2])
     median_x = last_x_diff[last_dir][2];
    else
      median_x = last_x_diff[last_dir][0];
  }
  else
  {
    if (last_x_diff[last_dir][0] < last_x_diff[last_dir][2])
      median_x = last_x_diff[last_dir][0];
    else if (last_x_diff[last_dir][1] < last_x_diff[last_dir][2])
      median_x = last_x_diff[last_dir][2];
    else
      median_x = last_x_diff[last_dir][1];
  }

  I32 median_y;
  if (last_y_diff[last_dir][0] < last_y_diff[last_dir][1])
  {
    if (last_y_diff[last_dir][1] < last_y_diff[last_dir][2])
      median_y = last_y_diff[last_dir][1];
    else if (last_y_diff[last_dir][0] < last_y_diff[last_dir][2])
      median_y = last_y_diff[last_dir][2];
    else
      median_y = last_y_diff[last_dir][0];
  }
  else
  {
    if (last_y_diff[last_dir][0] < last_y_diff[last_dir][2])
      median_y = last_y_diff[last_dir][0];
    else if (last_y_diff[last_dir][1] < last_y_diff[last_dir][2])
      median_y = last_y_diff[last_dir][2];
    else
      median_y = last_y_diff[last_dir][1];
  }

  // copy point from last point
  memcpy(item, last_item, 20);

  // decompress x y z coordinates
  I32 x_diff = ic_dx->decompress(median_x, last_dir);
  ((LASpoint10*)item)->x += x_diff;
  // we use the number k of bits corrector bits to switch contexts
  U32 k_bits = ic_dx->getK();
  I32 y_diff = ic_dy->decompress(median_y, k_bits);
  ((LASpoint10*)item)->y += y_diff;
  k_bits = (k_bits + ic_dy->getK())/2;
  ((LASpoint10*)item)->z = ic_z->decompress(((LASpoint10*)last_item)->z, k_bits);

	// decompress which other values have changed
	I32 changed_values = dec->decodeSymbol(m_changed_values);

  if (changed_values)
  {
		// decompress the intensity if it has changed
		if (changed_values & 32)
		{
			((LASpoint10*)item)->intensity = ic_intensity->decompress(((LASpoint10*)last_item)->intensity);
		}

		// decompress the edge_of_flight_line, scan_direction_flag, ... if it has changed
		if (changed_values & 16)
		{
			item[14] = dec->decodeSymbol(m_bit_byte);
		}

		// decompress the classification ... if it has changed
		if (changed_values & 8)
		{
			((LASpoint10*)item)->classification = dec->decodeSymbol(m_classification);
		}
		
		// decompress the scan_angle_rank ... if it has changed
		if (changed_values & 4)
		{
			((LASpoint10*)item)->scan_angle_rank = ic_scan_angle_rank->decompress(((LASpoint10*)last_item)->scan_angle_rank, k_bits < 3);
		}

		// decompress the user_data ... if it has changed
		if (changed_values & 2)
		{
			((LASpoint10*)item)->user_data = dec->decodeSymbol(m_user_data);
		}

		// decompress the point_source_ID ... if it has changed
		if (changed_values & 1)
		{
			((LASpoint10*)item)->point_source_ID = ic_point_source_ID->decompress(((LASpoint10*)last_item)->point_source_ID);
		}
  }

  // only record the difference if the scan direction has not changed
  if (last_dir == ((LASpoint10*)item)->scan_direction_flag)
  {
    last_x_diff[last_dir][last_incr[last_dir]] = x_diff;
    last_y_diff[last_dir][last_incr[last_dir]] = y_diff;
    last_incr[last_dir]++;
    if (last_incr[last_dir] > 2) last_incr[last_dir] = 0;
  }
  else
  {
    last_dir = ((LASpoint10*)item)->scan_direction_flag;
  }

  // copy the last point
  memcpy(last_item, item, 20);
  return TRUE;
}

/*
===============================================================================
                       LASreadItemCompressed_GPSTIME_v1
===============================================================================
*/

#define LASZIP_GPSTIME_MULTIMAX 512

LASreadItemCompressed_GPSTIME_v1::LASreadItemCompressed_GPSTIME_v1(EntropyDecoder* dec)
{
  /* set decoder */
  assert(dec);
  this->dec = dec;
  /* create entropy models and integer compressors */
  m_gpstime_multi = dec->createSymbolModel(LASZIP_GPSTIME_MULTIMAX);
  m_gpstime_0diff = dec->createSymbolModel(3);
  ic_gpstime = new IntegerCompressor(dec, 32, 6); // 32 bits, 6 contexts
}

LASreadItemCompressed_GPSTIME_v1::~LASreadItemCompressed_GPSTIME_v1()
{
  dec->destroySymbolModel(m_gpstime_multi);
  delete ic_gpstime;
}

BOOL LASreadItemCompressed_GPSTIME_v1::init(const U8* item)
{
  /* init state */
  last_gpstime_diff = 0;
  multi_extreme_counter = 0;

  /* init models and integer compressors */
  dec->initSymbolModel(m_gpstime_multi);
  dec->initSymbolModel(m_gpstime_0diff);
  ic_gpstime->initDecompressor();

  /* init last item */
  last_gpstime.f64 = *((F64*)item);
  return TRUE;
}

inline BOOL LASreadItemCompressed_GPSTIME_v1::read(U8* item)
{
  I32 multi;
  if (last_gpstime_diff == 0) // if the last integer difference was zero
  {
    multi = dec->decodeSymbol(m_gpstime_0diff);
    if (multi == 1) // the difference can be represented with 32 bits
    {
      last_gpstime_diff = ic_gpstime->decompress(0, 0);
      last_gpstime.i64 += last_gpstime_diff;      
    }
    else if (multi == 2) // the difference is huge
    {
      last_gpstime.f64 = dec->readDouble();
    }
  }
  else
  {
    multi = dec->decodeSymbol(m_gpstime_multi);

    if (multi <  LASZIP_GPSTIME_MULTIMAX-2)
    {
      I32 gpstime_diff;
      if (multi == 1)
      {
        gpstime_diff = ic_gpstime->decompress(last_gpstime_diff, 1);
        last_gpstime_diff = gpstime_diff;
        multi_extreme_counter = 0;
      }
	    else if (multi == 0)
	    {
        gpstime_diff = ic_gpstime->decompress(last_gpstime_diff/4, 2);
        multi_extreme_counter++;
	      if (multi_extreme_counter > 3)
	      {
	        last_gpstime_diff = gpstime_diff;
	        multi_extreme_counter = 0;
	      }
	    }
	    else if (multi < 10)
	    {
	      gpstime_diff = ic_gpstime->decompress(multi*last_gpstime_diff, 3);
	    }
      else if (multi < 50)
      {
	      gpstime_diff = ic_gpstime->decompress(multi*last_gpstime_diff, 4);
	    }
      else
      {
	      gpstime_diff = ic_gpstime->decompress(multi*last_gpstime_diff, 5);
	      if (multi == LASZIP_GPSTIME_MULTIMAX-3)
	      {
	        multi_extreme_counter++;
	        if (multi_extreme_counter > 3)
	        {
	          last_gpstime_diff = gpstime_diff;
	          multi_extreme_counter = 0;
	        }
	      }
      }
	    last_gpstime.i64 += gpstime_diff;
    }
    else if (multi <  LASZIP_GPSTIME_MULTIMAX-1)
    {
      last_gpstime.f64 = dec->readDouble();
    }
  }
  *((F64*)item) = last_gpstime.f64;
  return TRUE;
}

/*
===============================================================================
                       LASreadItemCompressed_RGB_v1
===============================================================================
*/

LASreadItemCompressed_RGB_v1::LASreadItemCompressed_RGB_v1(EntropyDecoder* dec)
{
  /* set decoder */
  assert(dec);
  this->dec = dec;

  /* create models and integer compressors */
  m_byte_used = dec->createSymbolModel(64);
  ic_rgb = new IntegerCompressor(dec, 8, 6);

  /* create last item */
  last_item = new U8[6];
}

LASreadItemCompressed_RGB_v1::~LASreadItemCompressed_RGB_v1()
{
  dec->destroySymbolModel(m_byte_used);
  delete ic_rgb;
  delete [] last_item;
}

BOOL LASreadItemCompressed_RGB_v1::init(const U8* item)
{
  /* init state */

  /* init models and integer compressors */
  dec->initSymbolModel(m_byte_used);
  ic_rgb->initDecompressor();

  /* init last item */
  memcpy(last_item, item, 6);
  return TRUE;
}

inline BOOL LASreadItemCompressed_RGB_v1::read(U8* item)
{
  U32 i, sym = dec->decodeSymbol(m_byte_used);
  for (i = 0; i < 6; i++)
  {
    if (sym & (1 << i))
    {
      item[i] = ic_rgb->decompress(last_item[i], i);
      last_item[i] = item[i];
    }
  }
  return TRUE;
}

/*
===============================================================================
                       LASreadItemCompressed_BYTE_v1
===============================================================================
*/

LASreadItemCompressed_BYTE_v1::LASreadItemCompressed_BYTE_v1(EntropyDecoder* dec, U32 number)
{
  /* set decoder */
  assert(dec);
  this->dec = dec;
  assert(number);
  this->number = number;

  /* create models and integer compressors */
  ic_byte = new IntegerCompressor(dec, 8, number);

  /* create last item */
  last_item = new U8[number];
}

LASreadItemCompressed_BYTE_v1::~LASreadItemCompressed_BYTE_v1()
{
  delete ic_byte;
  delete [] last_item;
}

BOOL LASreadItemCompressed_BYTE_v1::init(const U8* item)
{
  /* init state */

  /* init models and integer compressors */
  ic_byte->initDecompressor();

  /* init last item */
  memcpy(last_item, item, number);
  return TRUE;
}

inline BOOL LASreadItemCompressed_BYTE_v1::read(U8* item)
{
  U32 i;
  for (i = 0; i < number; i++)
  {
  	item[i] = (U8)(ic_byte->decompress(last_item[i], i));
  }
  memcpy(last_item, item, number);
  return TRUE;
}

