/* $Id$ */

/*
 * Copyright (c) 1988-1997 Sam Leffler
 * Copyright (c) 1991-1997 Silicon Graphics, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Sam Leffler and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Sam Leffler and Silicon Graphics.
 *
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/*
 * TIFF Library.
 *
 * Directory Read Support Routines.
 */
#include "tiffiop.h"

#define	IGNORE	0		/* tag placeholder used below */

#ifdef HAVE_IEEEFP
# define	TIFFCvtIEEEFloatToNative(tif, n, fp)
# define	TIFFCvtIEEEDoubleToNative(tif, n, dp)
#else
extern void TIFFCvtIEEEFloatToNative(TIFF*, uint32, float*);
extern void TIFFCvtIEEEDoubleToNative(TIFF*, uint32, double*);
#endif

static int EstimateStripByteCounts(TIFF*, TIFFDirEntryUnion*, uint16);
static void MissingRequired(TIFF*, const char*);
static int TIFFCheckDirOffset(TIFF* tif, uint64 diroff);
static int CheckDirCount(TIFF*, TIFFDirEntryUnion*, uint32);
static uint16 TIFFFetchDirectory(TIFF* tif, uint64 diroff, TIFFDirEntryUnion** pdir, uint64* nextdiroff);
static uint32 TIFFFetchData(TIFF*, TIFFDirEntryUnion*, char*);
static uint32 TIFFFetchString(TIFF*, TIFFDirEntryUnion*, char*);
static float TIFFFetchRational(TIFF*, TIFFDirEntryUnion*);
static int TIFFFetchNormalTag(TIFF*, TIFFDirEntryUnion*);
static int TIFFFetchPerSampleShorts(TIFF*, TIFFDirEntryUnion*, uint16*);
static int TIFFFetchPerSampleLongs(TIFF*, TIFFDirEntryUnion*, uint32*);
static int TIFFFetchPerSampleAnys(TIFF*, TIFFDirEntryUnion*, double*);
static int TIFFFetchShortArray(TIFF*, TIFFDirEntryUnion*, uint16*);
static int TIFFFetchStripThing(TIFF*, TIFFDirEntryUnion*, long, uint64**);
static int TIFFFetchRefBlackWhite(TIFF*, TIFFDirEntryUnion*);
static int TIFFFetchSubjectDistance(TIFF*, TIFFDirEntryUnion*);
static float TIFFFetchFloat(TIFF*, TIFFDirEntryUnion*);
static int TIFFFetchFloatArray(TIFF*, TIFFDirEntryUnion*, float*);
static int TIFFFetchDoubleArray(TIFF*, TIFFDirEntryUnion*, double*);
static int TIFFFetchAnyArray(TIFF*, TIFFDirEntryUnion*, double*);
static int TIFFFetchShortPair(TIFF*, TIFFDirEntryUnion*);
static void ChopUpSingleUncompressedStrip(TIFF*);

/* dddddddddddddddddddddddddd */

enum TIFFReadDirEntryErr {
	TIFFReadDirEntryErrOk = 0,
	TIFFReadDirEntryErrCount = 1,
	TIFFReadDirEntryErrType = 2,
	TIFFReadDirEntryErrIo = 3,
	TIFFReadDirEntryErrRange = 4,
	TIFFReadDirEntryErrPsdif = 5,
	TIFFReadDirEntryErrSizesan = 6,
	TIFFReadDirEntryErrAlloc = 7,
};

static enum TIFFReadDirEntryErr TIFFReadDirEntryShort(TIFF* tif, TIFFDirEntryUnion* direntry, uint16* value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryLong(TIFF* tif, TIFFDirEntryUnion* direntry, uint32* value);

static enum TIFFReadDirEntryErr TIFFReadDirEntryArray(TIFF* tif, TIFFDirEntryUnion* direntry, uint32* count, void** value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryByteArray(TIFF* tif, TIFFDirEntryUnion* direntry, uint8** value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryShortArray(TIFF* tif, TIFFDirEntryUnion* direntry, uint16** value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryLong8Array(TIFF* tif, TIFFDirEntryUnion* direntry, uint64** value);

static enum TIFFReadDirEntryErr TIFFReadDirEntryPersampleShort(TIFF* tif, TIFFDirEntryUnion* direntry, uint16* value);

static void TIFFReadDirEntryCheckedByte(TIFF* tif, TIFFDirEntryUnion* direntry, uint8* value);
static void TIFFReadDirEntryCheckedSbyte(TIFF* tif, TIFFDirEntryUnion* direntry, int8* value);
static void TIFFReadDirEntryCheckedShort(TIFF* tif, TIFFDirEntryUnion* direntry, uint16* value);
static void TIFFReadDirEntryCheckedSshort(TIFF* tif, TIFFDirEntryUnion* direntry, int16* value);
static void TIFFReadDirEntryCheckedLong(TIFF* tif, TIFFDirEntryUnion* direntry, uint32* value);
static void TIFFReadDirEntryCheckedSlong(TIFF* tif, TIFFDirEntryUnion* direntry, int32* value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckedLong8(TIFF* tif, TIFFDirEntryUnion* direntry, uint64* value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckedSlong8(TIFF* tif, TIFFDirEntryUnion* direntry, int64* value);

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteSbyte(int8 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteShort(uint16 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteSshort(int16 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteLong(uint32 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteSlong(int32 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteLong8(uint64 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteSlong8(int64 value);

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeShortSbyte(int8 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeShortSshort(int16 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeShortLong(uint32 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeShortSlong(int32 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeShortLong8(uint64 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeShortSlong8(int64 value);

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLongSbyte(int8 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLongSshort(int16 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLongLong8(uint64 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLongSlong8(int64 value);

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLong8Sbyte(int8 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLong8Sshort(int16 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLong8Slong(int32 value);
static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLong8Slong8(int64 value);

static enum TIFFReadDirEntryErr TIFFReadDirEntryData(TIFF* tif, uint64 offset, uint32 size, void* dest);
static void TIFFReadDirEntryOutputErr(TIFF* tif, enum TIFFReadDirEntryErr err, char* tagname);

static void TIFFReadDirectoryCheckOrder(TIFF* tif, TIFFDirEntryUnion* dir, uint16 dircount);
static TIFFDirEntryUnion* TIFFReadDirectoryFindEntry(TIFF* tif, TIFFDirEntryUnion* dir, uint16 dircount, uint16 tagid);
static void TIFFReadDirectoryFindFieldInfo(TIFF* tif, uint16 tagid, uint16* fii);

static enum TIFFReadDirEntryErr TIFFReadDirEntryShort(TIFF* tif, TIFFDirEntryUnion* direntry, uint16* value)
{
	enum TIFFReadDirEntryErr err;
	uint64 count;
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		count=direntry->classic.tdir_count;
	else
		count=direntry->big.tdir_count;
	if (count!=1)
		return(TIFFReadDirEntryErrCount);
	switch (direntry->common.tdir_type)
	{
		case TIFF_BYTE:
			{
				uint8 m;
				TIFFReadDirEntryCheckedByte(tif,direntry,&m);
				*value=(uint16)m;
				return(TIFFReadDirEntryErrOk);
			}
		case TIFF_SBYTE:
			{
				int8 m;
				TIFFReadDirEntryCheckedSbyte(tif,direntry,&m);
				err=TIFFReadDirEntryCheckRangeShortSbyte(m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				*value=(uint16)m;
				return(TIFFReadDirEntryErrOk);
			}
		case TIFF_SHORT:
			TIFFReadDirEntryCheckedShort(tif,direntry,value);
			return(TIFFReadDirEntryErrOk);
		case TIFF_SSHORT:
			{
				int16 m;
				TIFFReadDirEntryCheckedSshort(tif,direntry,&m);
				err=TIFFReadDirEntryCheckRangeShortSshort(m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				*value=(uint16)m;
				return(TIFFReadDirEntryErrOk);
			}
		case TIFF_LONG:
			{
				uint32 m;
				TIFFReadDirEntryCheckedLong(tif,direntry,&m);
				err=TIFFReadDirEntryCheckRangeShortLong(m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				*value=(uint16)m;
				return(TIFFReadDirEntryErrOk);
			}
		case TIFF_SLONG:
			{
				int32 m;
				TIFFReadDirEntryCheckedSlong(tif,direntry,&m);
				err=TIFFReadDirEntryCheckRangeShortSlong(m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				*value=(uint16)m;
				return(TIFFReadDirEntryErrOk);
			}
		case TIFF_LONG8:
			{
				uint64 m;
				err=TIFFReadDirEntryCheckedLong8(tif,direntry,&m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				err=TIFFReadDirEntryCheckRangeShortLong8(m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				*value=(uint16)m;
				return(TIFFReadDirEntryErrOk);
			}
		case TIFF_SLONG8:
			{
				int64 m;
				err=TIFFReadDirEntryCheckedSlong8(tif,direntry,&m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				err=TIFFReadDirEntryCheckRangeShortSlong8(m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				*value=(uint16)m;
				return(TIFFReadDirEntryErrOk);
			}
		default:
			return(TIFFReadDirEntryErrType);
	}
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryLong(TIFF* tif, TIFFDirEntryUnion* direntry, uint32* value)
{
	enum TIFFReadDirEntryErr err;
	uint64 count;
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		count=direntry->classic.tdir_count;
	else
		count=direntry->big.tdir_count;
	if (count!=1)
		return(TIFFReadDirEntryErrCount);
	switch (direntry->common.tdir_type)
	{
		case TIFF_BYTE:
			{
				uint8 m;
				TIFFReadDirEntryCheckedByte(tif,direntry,&m);
				*value=(uint32)m;
				return(TIFFReadDirEntryErrOk);
			}
		case TIFF_SBYTE:
			{
				int8 m;
				TIFFReadDirEntryCheckedSbyte(tif,direntry,&m);
				err=TIFFReadDirEntryCheckRangeLongSbyte(m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				*value=(uint32)m;
				return(TIFFReadDirEntryErrOk);
			}
		case TIFF_SHORT:
			{
				uint16 m;
				TIFFReadDirEntryCheckedShort(tif,direntry,&m);
				*value=(uint32)m;
				return(TIFFReadDirEntryErrOk);
			}
		case TIFF_SSHORT:
			{
				int16 m;
				TIFFReadDirEntryCheckedSshort(tif,direntry,&m);
				err=TIFFReadDirEntryCheckRangeLongSshort(m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				*value=(uint32)m;
				return(TIFFReadDirEntryErrOk);
			}
		case TIFF_LONG:
			TIFFReadDirEntryCheckedLong(tif,direntry,value);
			return(TIFFReadDirEntryErrOk);
		case TIFF_SLONG:
			{
				int32 m;
				TIFFReadDirEntryCheckedSlong(tif,direntry,&m);
				err=TIFFReadDirEntryCheckRangeLongSshort(m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				*value=(uint32)m;
				return(TIFFReadDirEntryErrOk);
			}
		case TIFF_LONG8:
			{
				uint64 m;
				err=TIFFReadDirEntryCheckedLong8(tif,direntry,&m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				err=TIFFReadDirEntryCheckRangeLongLong8(m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				*value=(uint32)m;
				return(TIFFReadDirEntryErrOk);
			}
		case TIFF_SLONG8:
			{
				int64 m;
				err=TIFFReadDirEntryCheckedSlong8(tif,direntry,&m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				err=TIFFReadDirEntryCheckRangeLongSlong8(m);
				if (err!=TIFFReadDirEntryErrOk)
					return(err);
				*value=(uint32)m;
				return(TIFFReadDirEntryErrOk);
			}
		default:
			return(TIFFReadDirEntryErrType);
	}
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryArray(TIFF* tif, TIFFDirEntryUnion* direntry, uint32* count, void** value)
{
	uint64 count64;
	int typesize;
	uint32 datasize;
	void* data;
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		count64=direntry->classic.tdir_count;
	else
		count64=direntry->big.tdir_count;
	typesize=TIFFDataWidth(direntry->common.tdir_type);
	if ((count64==0)||(typesize==0))
	{
		*value=0;
		return(TIFFReadDirEntryErrOk);
	}
	if ((uint64)(4*1024*1024/typesize)<count64)
		return(TIFFReadDirEntryErrSizesan);
	*count=(uint32)count64;
	datasize=(*count)*typesize;
	data=_TIFFmalloc(datasize);
	if (data==0)
		return(TIFFReadDirEntryErrAlloc);
	if (!(tif->tif_flags&TIFF_BIGTIFF))
	{
		if (datasize<=4)
			_TIFFmemcpy(data,&direntry->classic.tdir_offset,datasize);
		else
		{
			enum TIFFReadDirEntryErr err;
			uint32 offset;
			offset=direntry->classic.tdir_offset.vu32;
			if (tif->tif_flags&TIFF_SWAB)
				TIFFSwabLong(&offset);
			err=TIFFReadDirEntryData(tif,offset,datasize,data);
			if (err!=TIFFReadDirEntryErrOk)
			{
				_TIFFfree(data);
				return(err);
			}
		}
	}
	else
	{
		if (datasize<=8)
			_TIFFmemcpy(data,&direntry->big.tdir_offset,datasize);
		else
		{
			enum TIFFReadDirEntryErr err;
			uint64 offset;
			offset=direntry->big.tdir_offset.vu64;
			if (tif->tif_flags&TIFF_SWAB)
				TIFFSwabLong8(&offset);
			err=TIFFReadDirEntryData(tif,offset,datasize,data);
			if (err!=TIFFReadDirEntryErrOk)
			{
				_TIFFfree(data);
				return(err);
			}
		}
	}
	*value=data;
	return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryByteArray(TIFF* tif, TIFFDirEntryUnion* direntry, uint8** value)
{
	enum TIFFReadDirEntryErr err;
	uint32 count;
	void* origdata;
	uint8* data;
	switch (direntry->common.tdir_type)
	{
		case TIFF_ASCII:
		case TIFF_UNDEFINED:
		case TIFF_BYTE:
		case TIFF_SBYTE:
		case TIFF_SHORT:
		case TIFF_SSHORT:
		case TIFF_LONG:
		case TIFF_SLONG:
		case TIFF_LONG8:
		case TIFF_SLONG8:
			break;
		default:
			return(TIFFReadDirEntryErrType);
	}
	err=TIFFReadDirEntryArray(tif,direntry,&count,&origdata);
	if (err!=TIFFReadDirEntryErrOk)
		return(err);
	switch (direntry->common.tdir_type)
	{
		case TIFF_ASCII:
		case TIFF_UNDEFINED:
		case TIFF_BYTE:
			*value=(uint8*)origdata;
			return(TIFFReadDirEntryErrOk);
		case TIFF_SBYTE:
			{
				int8* m;
				uint32 n;
				m=(int8*)origdata;
				for (n=0; n<count; n++)
				{
					err=TIFFReadDirEntryCheckRangeByteSbyte(*m);
					if (err!=TIFFReadDirEntryErrOk)
					{
						_TIFFfree(origdata);
						return(err);
					}
					m++;
				}
				*value=(uint8*)origdata;
				return(TIFFReadDirEntryErrOk);
			}
	}
	data=_TIFFmalloc(count);
	if (data==0)
	{
		_TIFFfree(origdata);
		return(TIFFReadDirEntryErrAlloc);
	}
	switch (direntry->common.tdir_type)
	{
		case TIFF_SHORT:
			{
				uint16* ma;
				uint8* mb;
				uint32 n;
				ma=(uint16*)origdata;
				mb=(uint8*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabShort(ma);
					err=TIFFReadDirEntryCheckRangeByteShort(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint8)(*ma++);
				}
			}
			break;
		case TIFF_SSHORT:
			{
				int16* ma;
				uint8* mb;
				uint32 n;
				ma=(int16*)origdata;
				mb=(uint8*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabShort((uint16*)ma);
					err=TIFFReadDirEntryCheckRangeByteSshort(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint8)(*ma++);
				}
			}
			break;
		case TIFF_LONG:
			{
				uint32* ma;
				uint8* mb;
				uint32 n;
				ma=(uint32*)origdata;
				mb=(uint8*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabLong(ma);
					err=TIFFReadDirEntryCheckRangeByteLong(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint8)(*ma++);
				}
			}
			break;
		case TIFF_SLONG:
			{
				int32* ma;
				uint8* mb;
				uint32 n;
				ma=(int32*)origdata;
				mb=(uint8*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabLong((uint32*)ma);
					err=TIFFReadDirEntryCheckRangeByteSlong(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint8)(*ma++);
				}
			}
			break;
		case TIFF_LONG8:
			{
				uint64* ma;
				uint8* mb;
				uint32 n;
				ma=(uint64*)origdata;
				mb=(uint8*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabLong8(ma);
					err=TIFFReadDirEntryCheckRangeByteLong8(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint8)(*ma++);
				}
			}
			break;
		case TIFF_SLONG8:
			{
				int64* ma;
				uint8* mb;
				uint32 n;
				ma=(int64*)origdata;
				mb=(uint8*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabLong8((uint64*)ma);
					err=TIFFReadDirEntryCheckRangeByteSlong8(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint8)(*ma++);
				}
			}
			break;
	}
	_TIFFfree(origdata);
	if (err!=TIFFReadDirEntryErrOk)
	{
		_TIFFfree(data);
		return(err);
	}
	*value=data;
	return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryShortArray(TIFF* tif, TIFFDirEntryUnion* direntry, uint16** value)
{
	enum TIFFReadDirEntryErr err;
	uint32 count;
	void* origdata;
	uint16* data;
	switch (direntry->common.tdir_type)
	{
		case TIFF_BYTE:
		case TIFF_SBYTE:
		case TIFF_SHORT:
		case TIFF_SSHORT:
		case TIFF_LONG:
		case TIFF_SLONG:
		case TIFF_LONG8:
		case TIFF_SLONG8:
			break;
		default:
			return(TIFFReadDirEntryErrType);
	}
	err=TIFFReadDirEntryArray(tif,direntry,&count,&origdata);
	if (err!=TIFFReadDirEntryErrOk)
		return(err);
	switch (direntry->common.tdir_type)
	{
		case TIFF_SHORT:
			*value=(uint16*)origdata;
			if (tif->tif_flags&TIFF_SWAB)
				TIFFSwabArrayOfShort(*value,count);
			return(TIFFReadDirEntryErrOk);
		case TIFF_SSHORT:
			{
				int16* m;
				uint32 n;
				m=(int16*)origdata;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabShort((uint16*)m);
					err=TIFFReadDirEntryCheckRangeShortSshort(*m);
					if (err!=TIFFReadDirEntryErrOk)
					{
						_TIFFfree(origdata);
						return(err);
					}
					m++;
				}
				*value=(uint16*)origdata;
				return(TIFFReadDirEntryErrOk);
			}
	}
	data=_TIFFmalloc(count*2);
	if (data==0)
	{
		_TIFFfree(origdata);
		return(TIFFReadDirEntryErrAlloc);
	}
	switch (direntry->common.tdir_type)
	{
		case TIFF_BYTE:
			{
				uint8* ma;
				uint16* mb;
				uint32 n;
				ma=(uint8*)origdata;
				mb=(uint16*)data;
				for (n=0; n<count; n++)
					*mb++=(uint16)(*ma++);
			}
			break;
		case TIFF_SBYTE:
			{
				int8* ma;
				uint16* mb;
				uint32 n;
				ma=(int8*)origdata;
				mb=(uint16*)data;
				for (n=0; n<count; n++)
				{
					err=TIFFReadDirEntryCheckRangeShortSbyte(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint16)(*ma++);
				}
			}
			break;
		case TIFF_LONG:
			{
				uint32* ma;
				uint16* mb;
				uint32 n;
				ma=(uint32*)origdata;
				mb=(uint16*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabLong(ma);
					err=TIFFReadDirEntryCheckRangeShortLong(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint16)(*ma++);
				}
			}
			break;
		case TIFF_SLONG:
			{
				int32* ma;
				uint16* mb;
				uint32 n;
				ma=(int32*)origdata;
				mb=(uint16*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabLong((uint32*)ma);
					err=TIFFReadDirEntryCheckRangeShortSlong(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint16)(*ma++);
				}
			}
			break;
		case TIFF_LONG8:
			{
				uint64* ma;
				uint16* mb;
				uint32 n;
				ma=(uint64*)origdata;
				mb=(uint16*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabLong8(ma);
					err=TIFFReadDirEntryCheckRangeShortLong8(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint16)(*ma++);
				}
			}
			break;
		case TIFF_SLONG8:
			{
				int64* ma;
				uint16* mb;
				uint32 n;
				ma=(int64*)origdata;
				mb=(uint16*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabLong8((uint64*)ma);
					err=TIFFReadDirEntryCheckRangeShortSlong8(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint16)(*ma++);
				}
			}
			break;
	}
	_TIFFfree(origdata);
	if (err!=TIFFReadDirEntryErrOk)
	{
		_TIFFfree(data);
		return(err);
	}
	*value=data;
	return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryLong8Array(TIFF* tif, TIFFDirEntryUnion* direntry, uint64** value)
{
	enum TIFFReadDirEntryErr err;
	uint32 count;
	void* origdata;
	uint64* data;
	switch (direntry->common.tdir_type)
	{
		case TIFF_BYTE:
		case TIFF_SBYTE:
		case TIFF_SHORT:
		case TIFF_SSHORT:
		case TIFF_LONG:
		case TIFF_SLONG:
		case TIFF_LONG8:
		case TIFF_SLONG8:
			break;
		default:
			return(TIFFReadDirEntryErrType);
	}
	err=TIFFReadDirEntryArray(tif,direntry,&count,&origdata);
	if (err!=TIFFReadDirEntryErrOk)
		return(err);
	switch (direntry->common.tdir_type)
	{
		case TIFF_LONG8:
			*value=(uint64*)origdata;
			if (tif->tif_flags&TIFF_SWAB)
				TIFFSwabArrayOfLong8(*value,count);
			return(TIFFReadDirEntryErrOk);
		case TIFF_SLONG8:
			{
				int64* m;
				uint32 n;
				m=(int64*)origdata;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabLong8((uint64*)m);
					err=TIFFReadDirEntryCheckRangeLong8Slong8(*m);
					if (err!=TIFFReadDirEntryErrOk)
					{
						_TIFFfree(origdata);
						return(err);
					}
					m++;
				}
				*value=(uint64*)origdata;
				return(TIFFReadDirEntryErrOk);
			}
	}
	data=_TIFFmalloc(count*8);
	if (data==0)
	{
		_TIFFfree(origdata);
		return(TIFFReadDirEntryErrAlloc);
	}
	switch (direntry->common.tdir_type)
	{
		case TIFF_BYTE:
			{
				uint8* ma;
				uint64* mb;
				uint32 n;
				ma=(uint8*)origdata;
				mb=(uint64*)data;
				for (n=0; n<count; n++)
					*mb++=(uint64)(*ma++);
			}
			break;
		case TIFF_SBYTE:
			{
				int8* ma;
				uint64* mb;
				uint32 n;
				ma=(int8*)origdata;
				mb=(uint64*)data;
				for (n=0; n<count; n++)
				{
					err=TIFFReadDirEntryCheckRangeLong8Sbyte(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint64)(*ma++);
				}
			}
			break;
		case TIFF_SHORT:
			{
				uint16* ma;
				uint64* mb;
				uint32 n;
				ma=(uint16*)origdata;
				mb=(uint64*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabShort(ma);
					*mb++=(uint64)(*ma++);
				}
			}
			break;
		case TIFF_SSHORT:
			{
				int16* ma;
				uint64* mb;
				uint32 n;
				ma=(int16*)origdata;
				mb=(uint64*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabShort((uint16*)ma);
					err=TIFFReadDirEntryCheckRangeLong8Sshort(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint64)(*ma++);
				}
			}
			break;
		case TIFF_LONG:
			{
				uint32* ma;
				uint64* mb;
				uint32 n;
				ma=(uint32*)origdata;
				mb=(uint64*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabLong(ma);
					*mb++=(uint64)(*ma++);
				}
			}
			break;
		case TIFF_SLONG:
			{
				int32* ma;
				uint64* mb;
				uint32 n;
				ma=(int32*)origdata;
				mb=(uint64*)data;
				for (n=0; n<count; n++)
				{
					if (tif->tif_flags&TIFF_SWAB)
						TIFFSwabLong((uint32*)ma);
					err=TIFFReadDirEntryCheckRangeLong8Slong(*ma);
					if (err!=TIFFReadDirEntryErrOk)
						break;
					*mb++=(uint64)(*ma++);
				}
			}
			break;
	}
	_TIFFfree(origdata);
	if (err!=TIFFReadDirEntryErrOk)
	{
		_TIFFfree(data);
		return(err);
	}
	*value=data;
	return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryPersampleShort(TIFF* tif, TIFFDirEntryUnion* direntry, uint16* value)
{
	enum TIFFReadDirEntryErr err;
	uint64 count;
	uint16* m;
	uint16* na;
	uint16 nb;
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		count=direntry->classic.tdir_count;
	else
		count=direntry->big.tdir_count;
	if (count!=(uint64)tif->tif_dir.td_samplesperpixel)
		return(TIFFReadDirEntryErrCount);
	err=TIFFReadDirEntryShortArray(tif,direntry,&m);
	if (err!=TIFFReadDirEntryErrOk)
		return(err);
	na=m;
	nb=tif->tif_dir.td_samplesperpixel;
	*value=*na++;
	nb--;
	while (nb>0)
	{
		if (*na++!=*value)
		{
			err=TIFFReadDirEntryErrPsdif;
			break;
		}
		nb--;
	}
	_TIFFfree(m);
	return(err);
}

static void TIFFReadDirEntryCheckedByte(TIFF* tif, TIFFDirEntryUnion* direntry, uint8* value)
{
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		*value=direntry->classic.tdir_offset.vu8[0];
	else
		*value=direntry->big.tdir_offset.vu8[0];
}

static void TIFFReadDirEntryCheckedSbyte(TIFF* tif, TIFFDirEntryUnion* direntry, int8* value)
{
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		*value=direntry->classic.tdir_offset.vs8[0];
	else
		*value=direntry->big.tdir_offset.vs8[0];
}

static void TIFFReadDirEntryCheckedShort(TIFF* tif, TIFFDirEntryUnion* direntry, uint16* value)
{
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		*value=direntry->classic.tdir_offset.vu16[0];
	else
		*value=direntry->big.tdir_offset.vu16[0];
	if (tif->tif_flags&TIFF_SWAB)
		TIFFSwabShort(value);
}

static void TIFFReadDirEntryCheckedSshort(TIFF* tif, TIFFDirEntryUnion* direntry, int16* value)
{
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		*value=direntry->classic.tdir_offset.vs16[0];
	else
		*value=direntry->big.tdir_offset.vs16[0];
	if (tif->tif_flags&TIFF_SWAB)
		TIFFSwabShort((uint16*)value);
}

static void TIFFReadDirEntryCheckedLong(TIFF* tif, TIFFDirEntryUnion* direntry, uint32* value)
{
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		*value=direntry->classic.tdir_offset.vu32;
	else
		*value=direntry->big.tdir_offset.vu32[0];
	if (tif->tif_flags&TIFF_SWAB)
		TIFFSwabLong(value);
}

static void TIFFReadDirEntryCheckedSlong(TIFF* tif, TIFFDirEntryUnion* direntry, int32* value)
{
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		*value=direntry->classic.tdir_offset.vs32;
	else
		*value=direntry->big.tdir_offset.vs32[0];
	if (tif->tif_flags&TIFF_SWAB)
		TIFFSwabLong((uint32*)value);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckedLong8(TIFF* tif, TIFFDirEntryUnion* direntry, uint64* value)
{
	if (!(tif->tif_flags&TIFF_BIGTIFF))
	{
		enum TIFFReadDirEntryErr err;
		err=TIFFReadDirEntryData(tif,direntry->classic.tdir_offset.vu32,8,value);
		if (err!=TIFFReadDirEntryErrOk)
			return(err);
	}
	else
		*value=direntry->big.tdir_offset.vu64;
	if (tif->tif_flags&TIFF_SWAB)
		TIFFSwabLong8(value);
	return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckedSlong8(TIFF* tif, TIFFDirEntryUnion* direntry, int64* value)
{
	if (!(tif->tif_flags&TIFF_BIGTIFF))
	{
		enum TIFFReadDirEntryErr err;
		err=TIFFReadDirEntryData(tif,direntry->classic.tdir_offset.vu32,8,value);
		if (err!=TIFFReadDirEntryErrOk)
			return(err);
	}
	else
		*value=direntry->big.tdir_offset.vs64;
	if (tif->tif_flags&TIFF_SWAB)
		TIFFSwabLong8((uint64*)value);
	return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteSbyte(int8 value)
{
	if (value<0)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteShort(uint16 value)
{
	if (value>0xFF)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteSshort(int16 value)
{
	if ((value<0)||(value>0xFF))
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteLong(uint32 value)
{
	if (value>0xFF)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteSlong(int32 value)
{
	if ((value<0)||(value>0xFF))
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteLong8(uint64 value)
{
	if (value>0xFF)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeByteSlong8(int64 value)
{
	if ((value<0)||(value>0xFF))
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeShortSbyte(int8 value)
{
	if (value<0)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeShortSshort(int16 value)
{
	if (value<0)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeShortLong(uint32 value)
{
	if (value>0xFFFF)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeShortSlong(int32 value)
{
	if ((value<0)||(value>0xFFFF))
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeShortLong8(uint64 value)
{
	if (value>0xFFFF)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeShortSlong8(int64 value)
{
	if ((value<0)||(value>0xFFFF))
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLongSbyte(int8 value)
{
	if (value<0)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLongSshort(int16 value)
{
	if (value<0)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLongLong8(uint64 value)
{
	if (value>0xFFFFFFFF)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLongSlong8(int64 value)
{
	if ((value<0)||(value>0xFFFFFFFF))
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLong8Sbyte(int8 value)
{
	if (value<0)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLong8Sshort(int16 value)
{
	if (value<0)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLong8Slong(int32 value)
{
	if (value<0)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryCheckRangeLong8Slong8(int64 value)
{
	if (value<0)
		return(TIFFReadDirEntryErrRange);
	else
		return(TIFFReadDirEntryErrOk);
}

static enum TIFFReadDirEntryErr TIFFReadDirEntryData(TIFF* tif, uint64 offset, uint32 size, void* dest)
{
	if (!isMapped(tif)) {
		if (!SeekOK(tif,offset))
			return(TIFFReadDirEntryErrIo);
		if (!ReadOK(tif,dest,size))
			return(TIFFReadDirEntryErrIo);
	} else {
		if ((offset+size<offset)||
		    (offset+size<size)||
		    (offset+size>tif->tif_size))
			return(TIFFReadDirEntryErrIo);
		_TIFFmemcpy(dest,tif->tif_base+offset,size);
	}
	return(TIFFReadDirEntryErrOk);
}

static void TIFFReadDirEntryOutputErr(TIFF* tif, enum TIFFReadDirEntryErr err, char* tagname)
{
	assert(0);
}

/*
 * Read the next TIFF directory from a file and convert it to the internal
 * format. We read directories sequentially.
 */
int
TIFFReadDirectory(TIFF* tif)
{
	static const char module[] = "TIFFReadDirectory";
	TIFFDirEntryUnion* dir;
	uint16 dircount;
	TIFFDirEntryUnion* dp;
	uint16 di;
	const TIFFFieldInfo* fip;
	uint16 fii;
	/*
	int n;
	TIFFDirectory* td;
	TIFFDirEntryUnion *dp, *dir = NULL;
	uint16 iv;
	const TIFFFieldInfo* fip;
	size_t fix;
	uint16 dircount;
	*/
	tif->tif_diroff=tif->tif_nextdiroff;
	if (!TIFFCheckDirOffset(tif, tif->tif_nextdiroff))
		return 0;           /* last offset or bad offset (IFD looping) */
	(*tif->tif_cleanup)(tif);   /* cleanup any previous compression state */
	tif->tif_curdir++;
	dircount=TIFFFetchDirectory(tif,tif->tif_nextdiroff,&dir,&tif->tif_nextdiroff);
	if (!dircount) {
		TIFFErrorExt(tif->tif_clientdata,module,
		     "Failed to read directory at offset %lu",tif->tif_nextdiroff);
		return 0;
	}
	TIFFReadDirectoryCheckOrder(tif,dir,dircount);
	tif->tif_flags&=~TIFF_BEENWRITING;    /* reset before new dir */
	/*
	 * Setup default value and then make a pass over
	 * the fields to check type and tag information,
	 * and to extract info required to size data
	 * structures.  A second pass is made afterwards
	 * to read in everthing not taken in the first pass.
	 */
	/* td = &tif->tif_dir; */
	/* free any old stuff and reinit */
	TIFFFreeDirectory(tif);
	TIFFDefaultDirectory(tif);
	/*
	 * Electronic Arts writes gray-scale TIFF files
	 * without a PlanarConfiguration directory entry.
	 * Thus we setup a default value here, even though
	 * the TIFF spec says there is no default value.
	 */
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	/* We must process the Compression tag first
	 * in order to merge in codec-private tag definitions (otherwise
	 * we may get complaints about unknown tags).  However, the
	 * Compression tag may be dependent on the SamplesPerPixel
	 * tag value because older TIFF specs permited Compression
	 * to be written as a SamplesPerPixel-count tag entry.
	 * Thus if we don't first figure out the correct SamplesPerPixel
	 * tag value then we may end up ignoring the Compression tag
	 * value because it has an incorrect count value (if the
	 * true value of SamplesPerPixel is not 1).
	 */
	dp=TIFFReadDirectoryFindEntry(tif,dir,dircount,TIFFTAG_SAMPLESPERPIXEL);
	if (dp)
	{
		if (!TIFFFetchNormalTag(tif,dp))
			goto bad;
		dp->common.tdir_tag=IGNORE;
	}
	dp=TIFFReadDirectoryFindEntry(tif,dir,dircount,TIFFTAG_COMPRESSION);
	if (dp)
	{
		/*
		 * The 5.0 spec says the Compression tag has
		 * one value, while earlier specs say it has
		 * one value per sample.  Because of this, we
		 * accept the tag if one value is supplied.
		 */
		uint16 value;
		enum TIFFReadDirEntryErr err;
		err=TIFFReadDirEntryShort(tif,dp,&value);
		if (err==TIFFReadDirEntryErrCount)
			err=TIFFReadDirEntryPersampleShort(tif,dp,&value);
		if (err!=TIFFReadDirEntryErrOk)
		{
			TIFFReadDirEntryOutputErr(tif,err,"Compression");
			goto bad;
		}
		if (!TIFFSetField(tif,TIFFTAG_COMPRESSION,value))
			goto bad;
		dp->common.tdir_tag=IGNORE;
	}
	else
	{
		if (!TIFFSetField(tif,TIFFTAG_COMPRESSION,COMPRESSION_NONE))
			goto bad;
	}
	/*
	 * First real pass over the directory.
	 */
	dp=dir;
	for (di=0; di<dircount; di++)
	{
		if (dp->common.tdir_tag!=IGNORE)
		{
			TIFFReadDirectoryFindFieldInfo(tif,dp->common.tdir_tag,&fii);
			if (fii==0xFFFF)
			{
				TIFFWarningExt(tif->tif_clientdata,module,
				    "Unknown field with tag %d (0x%x) encountered",
				    dp->common.tdir_tag,dp->common.tdir_tag);
				TIFFMergeFieldInfo(tif,
				    _TIFFCreateAnonFieldInfo(tif,dp->common.tdir_tag,
				    (TIFFDataType)dp->common.tdir_type),1);
				TIFFReadDirectoryFindFieldInfo(tif,dp->common.tdir_tag,&fii);
				assert(fii!=0xFFFF);
			}
			fip=tif->tif_fieldinfo[fii];
			if (fip->field_bit==FIELD_IGNORE)
				dp->common.tdir_tag=IGNORE;
			else
			{
				/* check data type */
				while ((fip->field_type!=TIFF_ANY)&&(fip->field_type!=dp->common.tdir_type))
				{
					fii++;
					if (fii==tif->tif_nfields)
					{
						fii=0xFFFF;
						break;
					}
					if (tif->tif_fieldinfo[fii]->field_tag!=dp->common.tdir_tag)
					{
						fii=0xFFFF;
						break;
					}
					fip=tif->tif_fieldinfo[fii];
				}
				if (fii==0xFFFF)
				{
					TIFFWarningExt(tif->tif_clientdata, module,
					    "Wrong data type %d for \"%s\"; tag ignored",
					    dp->common.tdir_type,fip->field_name);
					dp->common.tdir_tag=IGNORE;
				}
				else
				{
					/* check count if known in advance */
					if ((fip->field_readcount!=TIFF_VARIABLE)&&
					    (fip->field_readcount!=TIFF_VARIABLE2))
					{
						uint32 expected;
						if (fip->field_readcount==TIFF_SPP)
							expected=(uint32)tif->tif_dir.td_samplesperpixel;
						else
							expected=(uint32)fip->field_readcount;
						if (!CheckDirCount(tif,dp,expected))
							dp->common.tdir_tag=IGNORE;
					}
				}
			}
			switch (dp->common.tdir_tag)
			{
				case TIFFTAG_STRIPOFFSETS:
				case TIFFTAG_STRIPBYTECOUNTS:
				case TIFFTAG_TILEOFFSETS:
				case TIFFTAG_TILEBYTECOUNTS:
					TIFFSetFieldBit(tif,fip->field_bit);
					break;
				case TIFFTAG_IMAGEWIDTH:
				case TIFFTAG_IMAGELENGTH:
				case TIFFTAG_IMAGEDEPTH:
				case TIFFTAG_TILELENGTH:
				case TIFFTAG_TILEWIDTH:
				case TIFFTAG_TILEDEPTH:
				case TIFFTAG_PLANARCONFIG:
				case TIFFTAG_ROWSPERSTRIP:
				case TIFFTAG_EXTRASAMPLES:
					if (!TIFFFetchNormalTag(tif,dp))
						goto bad;
					dp->common.tdir_tag=IGNORE;
					break;
			}
		}
		if (!(tif->tif_flags&TIFF_BIGTIFF))
			(uint8*)dp+=sizeof(TIFFDirEntryClassic);
		else
			(uint8*)dp+=sizeof(TIFFDirEntryBig);
	}
	/*
	 * XXX: OJPEG hack.
	 * If a) compression is OJPEG, b) planarconfig tag says it's separate,
	 * c) strip offsets/bytecounts tag are both present and
	 * d) both contain exactly one value, then we consistently find
	 * that the buggy implementation of the buggy compression scheme
	 * matches contig planarconfig best. So we 'fix-up' the tag here
	 */
	if ((tif->tif_dir.td_compression==COMPRESSION_OJPEG) &&
	    (tif->tif_dir.td_planarconfig==PLANARCONFIG_SEPARATE)) {
		dp = TIFFReadDirectoryFindEntry(tif,dir,dircount,TIFFTAG_STRIPOFFSETS);
		if ((dp!=0) && (((tif->tif_flags&TIFF_BIGTIFF)?dp->classic.tdir_count:dp->big.tdir_count)==1)) {
			dp = TIFFReadDirectoryFindEntry(tif, dir, dircount,
						   TIFFTAG_STRIPBYTECOUNTS);
			if ((dp!=0) && (((tif->tif_flags&TIFF_BIGTIFF)?dp->classic.tdir_count:dp->big.tdir_count)==1)) {
				tif->tif_dir.td_planarconfig=PLANARCONFIG_CONTIG;
				TIFFWarningExt(tif->tif_clientdata,
					       "TIFFReadDirectory",
				"Planarconfig tag value assumed incorrect, "
				"assuming data is contig instead of chunky");
			}
		}
	}

	/*
	 * Allocate directory structure and setup defaults.
	 */
	if (!TIFFFieldSet(tif, FIELD_IMAGEDIMENSIONS)) {
		MissingRequired(tif, "ImageLength");
		goto bad;
	}
	/* 
	 * Setup appropriate structures (by strip or by tile)
	 */
	if (!TIFFFieldSet(tif, FIELD_TILEDIMENSIONS)) {
		tif->tif_dir.td_nstrips = TIFFNumberOfStrips(tif);
		tif->tif_dir.td_tilewidth = tif->tif_dir.td_imagewidth;
		tif->tif_dir.td_tilelength = tif->tif_dir.td_rowsperstrip;
		tif->tif_dir.td_tiledepth = tif->tif_dir.td_imagedepth;
		tif->tif_flags &= ~TIFF_ISTILED;
	} else {
		tif->tif_dir.td_nstrips = TIFFNumberOfTiles(tif);
		tif->tif_flags |= TIFF_ISTILED;
	}
	if (!tif->tif_dir.td_nstrips) {
		TIFFErrorExt(tif->tif_clientdata, module,
			     "%s: cannot handle zero number of %s",
			     tif->tif_name, isTiled(tif) ? "tiles" : "strips");
		goto bad;
	}
	tif->tif_dir.td_stripsperimage = tif->tif_dir.td_nstrips;
	if (tif->tif_dir.td_planarconfig == PLANARCONFIG_SEPARATE)
		tif->tif_dir.td_stripsperimage /= tif->tif_dir.td_samplesperpixel;
	if (!TIFFFieldSet(tif, FIELD_STRIPOFFSETS)) {
		if ((tif->tif_dir.td_compression==COMPRESSION_OJPEG) &&
		    (isTiled(tif)==0) &&
		    (tif->tif_dir.td_nstrips==1)) {
			/*
			 * XXX: OJPEG hack.
			 * If a) compression is OJPEG, b) it's not a tiled TIFF,
			 * and c) the number of strips is 1,
			 * then we tolerate the absence of stripoffsets tag,
			 * because, presumably, all required data is in the
			 * JpegInterchangeFormat stream.
			 */
			TIFFSetFieldBit(tif, FIELD_STRIPOFFSETS);
		} else {
			MissingRequired(tif,
				isTiled(tif) ? "TileOffsets" : "StripOffsets");
			goto bad;
		}
	}

	/*
	 * Second pass: extract other information.
	 */
	dp=dir;
	for (di=0; di<dircount; di++)
	{
		switch (dp->common.tdir_tag)
		{
			case IGNORE:
				break;
			case TIFFTAG_MINSAMPLEVALUE:
			case TIFFTAG_MAXSAMPLEVALUE:
			case TIFFTAG_BITSPERSAMPLE:
			case TIFFTAG_DATATYPE:
			case TIFFTAG_SAMPLEFORMAT:
				/*
				 * The MinSampleValue, MaxSampleValue, BitsPerSample
				 * DataType and SampleFormat tags are supposed to be
				 * written as one value/sample, but some vendors
				 * incorrectly write one value only -- so we accept
				 * that as well (yech). Other vendors write correct
				 * value for NumberOfSamples, but incorrect one for
				 * BitsPerSample and friends, and we will read this
				 * too.
				 */
				{
					uint16 value;
					enum TIFFReadDirEntryErr err;
					err=TIFFReadDirEntryShort(tif,dp,&value);
					if (err==TIFFReadDirEntryErrCount)
						err=TIFFReadDirEntryPersampleShort(tif,dp,&value);
					if (err!=TIFFReadDirEntryErrOk)
					{
						TIFFReadDirEntryOutputErr(tif,err,_TIFFFieldWithTag(tif,dir->common.tdir_tag)->field_name);
						goto bad;
					}
					if (!TIFFSetField(tif,dp->common.tdir_tag,value))
						goto bad;
				}
				break;
			case TIFFTAG_SMINSAMPLEVALUE:
			case TIFFTAG_SMAXSAMPLEVALUE:
				{
					double dv = 0.0;
					if (!TIFFFetchPerSampleAnys(tif, dp, &dv) ||
					    !TIFFSetField(tif, dp->common.tdir_tag, dv))
						goto bad;
				}
				break;
			case TIFFTAG_STRIPOFFSETS:
			case TIFFTAG_TILEOFFSETS:
				if (!TIFFFetchStripThing(tif, dp,
				    tif->tif_dir.td_nstrips, &tif->tif_dir.td_stripoffset))
					goto bad;
				break;
			case TIFFTAG_STRIPBYTECOUNTS:
			case TIFFTAG_TILEBYTECOUNTS:
				if (!TIFFFetchStripThing(tif, dp,
				    tif->tif_dir.td_nstrips, &tif->tif_dir.td_stripbytecount))
					goto bad;
				break;
			case TIFFTAG_COLORMAP:
			case TIFFTAG_TRANSFERFUNCTION:
				{
					uint32 v;
					char* cp;
					/*
					 * TransferFunction can have either 1x or 3x
					 * data values; Colormap can have only 3x
					 * items.
					 */
					assert(0);
					#ifdef NDEF
					v = 1L<<tif->tif_dir.td_bitspersample;
					if (dp->common.tdir_tag == TIFFTAG_COLORMAP ||
					    dp->tdir_count != v) {
						if (!CheckDirCount(tif, dp, 3 * v))
							break;
					}
					v *= sizeof(uint16);
					cp = (char *)_TIFFCheckMalloc(tif,
								      dp->tdir_count,
								      sizeof (uint16),
						"to read \"TransferFunction\" tag");
					if (cp != NULL) {
						if (TIFFFetchData(tif, dp, cp)) {
							/*
							 * This deals with there being
							 * only one array to apply to
							 * all samples.
							 */
							uint32 c = 1L << td->td_bitspersample;
							if (dp->tdir_count == c)
								v = 0L;
							TIFFSetField(tif, dp->tdir_tag,
							    cp, cp+v, cp+2*v);
						}
						_TIFFfree(cp);
					}
					#endif
					break;
				}
			case TIFFTAG_PAGENUMBER:
			case TIFFTAG_HALFTONEHINTS:
			case TIFFTAG_YCBCRSUBSAMPLING:
			case TIFFTAG_DOTRANGE:
				(void) TIFFFetchShortPair(tif, dp);
				break;
			case TIFFTAG_REFERENCEBLACKWHITE:
				(void) TIFFFetchRefBlackWhite(tif, dp);
				break;
/* BEGIN REV 4.0 COMPATIBILITY */
			case TIFFTAG_OSUBFILETYPE:
				assert(0);
				#ifdef NDEF
				v = 0L;
				switch (TIFFExtractData(tif, dp->tdir_type,
				    dp->tdir_offset)) {
				case OFILETYPE_REDUCEDIMAGE:
					v = FILETYPE_REDUCEDIMAGE;
					break;
				case OFILETYPE_PAGE:
					v = FILETYPE_PAGE;
					break;
				}
				if (v)
					TIFFSetField(tif, TIFFTAG_SUBFILETYPE, v);
				#endif
				break;
/* END REV 4.0 COMPATIBILITY */
			default:
				(void) TIFFFetchNormalTag(tif, dp);
				break;
		}
		if (!(tif->tif_flags&TIFF_BIGTIFF))
			(uint8*)dp+=sizeof(TIFFDirEntryClassic);
		else
			(uint8*)dp+=sizeof(TIFFDirEntryBig);
	}
	/*
	 * OJPEG hack:
	 * - If a) compression is OJPEG, and b) photometric tag is missing,
	 * then we consistently find that photometric should be YCbCr
	 * - If a) compression is OJPEG, and b) photometric tag says it's RGB,
	 * then we consistently find that the buggy implementation of the
	 * buggy compression scheme matches photometric YCbCr instead.
	 * - If a) compression is OJPEG, and b) bitspersample tag is missing,
	 * then we consistently find bitspersample should be 8.
	 * - If a) compression is OJPEG, b) samplesperpixel tag is missing,
	 * and c) photometric is RGB or YCbCr, then we consistently find
	 * samplesperpixel should be 3
	 * - If a) compression is OJPEG, b) samplesperpixel tag is missing,
	 * and c) photometric is MINISWHITE or MINISBLACK, then we consistently
	 * find samplesperpixel should be 3
	 */
	if (tif->tif_dir.td_compression==COMPRESSION_OJPEG)
	{
		if (!TIFFFieldSet(tif,FIELD_PHOTOMETRIC))
		{
			TIFFWarningExt(tif->tif_clientdata, "TIFFReadDirectory",
			"Photometric tag is missing, assuming data is YCbCr");
			if (!TIFFSetField(tif,TIFFTAG_PHOTOMETRIC,PHOTOMETRIC_YCBCR))
				goto bad;
		}
		else if (tif->tif_dir.td_photometric==PHOTOMETRIC_RGB)
		{
			tif->tif_dir.td_photometric=PHOTOMETRIC_YCBCR;
			TIFFWarningExt(tif->tif_clientdata, "TIFFReadDirectory",
			"Photometric tag value assumed incorrect, "
			"assuming data is YCbCr instead of RGB");
		}
		if (!TIFFFieldSet(tif,FIELD_BITSPERSAMPLE))
		{
			TIFFWarningExt(tif->tif_clientdata,"TIFFReadDirectory",
		"BitsPerSample tag is missing, assuming 8 bits per sample");
			if (!TIFFSetField(tif,TIFFTAG_BITSPERSAMPLE,8))
				goto bad;
		}
		if (!TIFFFieldSet(tif,FIELD_SAMPLESPERPIXEL))
		{
			if ((tif->tif_dir.td_photometric==PHOTOMETRIC_RGB)
			    || (tif->tif_dir.td_photometric==PHOTOMETRIC_YCBCR))
			{
				TIFFWarningExt(tif->tif_clientdata,
					       "TIFFReadDirectory",
				"SamplesPerPixel tag is missing, "
				"assuming correct SamplesPerPixel value is 3");
				if (!TIFFSetField(tif,TIFFTAG_SAMPLESPERPIXEL,3))
					goto bad;
			}
			else if ((tif->tif_dir.td_photometric==PHOTOMETRIC_MINISWHITE)
				 || (tif->tif_dir.td_photometric==PHOTOMETRIC_MINISBLACK))
			{
				TIFFWarningExt(tif->tif_clientdata,
					       "TIFFReadDirectory",
				"SamplesPerPixel tag is missing, "
				"assuming correct SamplesPerPixel value is 1");
				if (!TIFFSetField(tif,TIFFTAG_SAMPLESPERPIXEL,1))
					goto bad;
			}
		}
	}
	/*
	 * Verify Palette image has a Colormap.
	 */
	if (tif->tif_dir.td_photometric == PHOTOMETRIC_PALETTE &&
	    !TIFFFieldSet(tif, FIELD_COLORMAP)) {
		MissingRequired(tif, "Colormap");
		goto bad;
	}
	/*
	 * OJPEG hack:
	 * We do no further messing with strip/tile offsets/bytecounts in OJPEG
	 * TIFFs
	 */
	if (tif->tif_dir.td_compression!=COMPRESSION_OJPEG)
	{
		/*
		 * Attempt to deal with a missing StripByteCounts tag.
		 */
		if (!TIFFFieldSet(tif, FIELD_STRIPBYTECOUNTS)) {
			/*
			 * Some manufacturers violate the spec by not giving
			 * the size of the strips.  In this case, assume there
			 * is one uncompressed strip of data.
			 */
			if ((tif->tif_dir.td_planarconfig == PLANARCONFIG_CONTIG &&
			    tif->tif_dir.td_nstrips > 1) ||
			    (tif->tif_dir.td_planarconfig == PLANARCONFIG_SEPARATE &&
			     tif->tif_dir.td_nstrips != tif->tif_dir.td_samplesperpixel)) {
			    MissingRequired(tif, "StripByteCounts");
			    goto bad;
			}
			TIFFWarningExt(tif->tif_clientdata, module,
				"%s: TIFF directory is missing required "
				"\"%s\" field, calculating from imagelength",
				tif->tif_name,
				_TIFFFieldWithTag(tif,TIFFTAG_STRIPBYTECOUNTS)->field_name);
			if (EstimateStripByteCounts(tif, dir, dircount) < 0)
			    goto bad;
		/*
		 * Assume we have wrong StripByteCount value (in case
		 * of single strip) in following cases:
		 *   - it is equal to zero along with StripOffset;
		 *   - it is larger than file itself (in case of uncompressed
		 *     image);
		 *   - it is smaller than the size of the bytes per row
		 *     multiplied on the number of rows.  The last case should
		 *     not be checked in the case of writing new image,
		 *     because we may do not know the exact strip size
		 *     until the whole image will be written and directory
		 *     dumped out.
		 */
		#define	BYTECOUNTLOOKSBAD \
		    ( (tif->tif_dir.td_stripbytecount[0] == 0 && tif->tif_dir.td_stripoffset[0] != 0) || \
		      (tif->tif_dir.td_compression == COMPRESSION_NONE && \
		       tif->tif_dir.td_stripbytecount[0] > TIFFGetFileSize(tif) - tif->tif_dir.td_stripoffset[0]) || \
		      (tif->tif_mode == O_RDONLY && \
		       tif->tif_dir.td_compression == COMPRESSION_NONE && \
		       tif->tif_dir.td_stripbytecount[0] < TIFFScanlineSize(tif) * tif->tif_dir.td_imagelength) )

		} else if (tif->tif_dir.td_nstrips == 1
			   && tif->tif_dir.td_stripoffset[0] != 0
			   && BYTECOUNTLOOKSBAD) {
			/*
			 * XXX: Plexus (and others) sometimes give a value of
			 * zero for a tag when they don't know what the
			 * correct value is!  Try and handle the simple case
			 * of estimating the size of a one strip image.
			 */
			TIFFWarningExt(tif->tif_clientdata, module,
	"%s: Bogus \"%s\" field, ignoring and calculating from imagelength",
				    tif->tif_name,
				    _TIFFFieldWithTag(tif,TIFFTAG_STRIPBYTECOUNTS)->field_name);
			if(EstimateStripByteCounts(tif, dir, dircount) < 0)
			    goto bad;
		} else if (tif->tif_dir.td_planarconfig == PLANARCONFIG_CONTIG
			   && tif->tif_dir.td_nstrips > 2
			   && tif->tif_dir.td_compression == COMPRESSION_NONE
			   && tif->tif_dir.td_stripbytecount[0] != tif->tif_dir.td_stripbytecount[1]
			   && tif->tif_dir.td_stripbytecount[0] != 0
                           && tif->tif_dir.td_stripbytecount[1] != 0 ) {
			/*
			 * XXX: Some vendors fill StripByteCount array with 
                         * absolutely wrong values (it can be equal to 
                         * StripOffset array, for example). Catch this case 
                         * here.
			 */
			TIFFWarningExt(tif->tif_clientdata, module,
	"%s: Wrong \"%s\" field, ignoring and calculating from imagelength",
				    tif->tif_name,
				    _TIFFFieldWithTag(tif,TIFFTAG_STRIPBYTECOUNTS)->field_name);
			if (EstimateStripByteCounts(tif, dir, dircount) < 0)
			    goto bad;
		}
	}
	if (dir) {
		_TIFFfree((char *)dir);
		dir = NULL;
	}
	if (!TIFFFieldSet(tif, FIELD_MAXSAMPLEVALUE))
		tif->tif_dir.td_maxsamplevalue = (uint16)((1L<<tif->tif_dir.td_bitspersample)-1);
	/*
	 * Setup default compression scheme.
	 */

	/*
	 * XXX: We can optimize checking for the strip bounds using the sorted
	 * bytecounts array. See also comments for TIFFAppendToStrip()
	 * function in tif_write.c.
	 */
	if (tif->tif_dir.td_nstrips > 1) {
		tstrip_t strip;

		tif->tif_dir.td_stripbytecountsorted = 1;
		for (strip = 1; strip < tif->tif_dir.td_nstrips; strip++) {
			if (tif->tif_dir.td_stripoffset[strip - 1] >
			    tif->tif_dir.td_stripoffset[strip]) {
				tif->tif_dir.td_stripbytecountsorted = 0;
				break;
			}
		}
	}

	if (!TIFFFieldSet(tif, FIELD_COMPRESSION))
		TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
	/*
	 * Some manufacturers make life difficult by writing
	 * large amounts of uncompressed data as a single strip.
	 * This is contrary to the recommendations of the spec.
	 * The following makes an attempt at breaking such images
	 * into strips closer to the recommended 8k bytes.  A
	 * side effect, however, is that the RowsPerStrip tag
	 * value may be changed.
	 */
	if (tif->tif_dir.td_nstrips == 1 && tif->tif_dir.td_compression == COMPRESSION_NONE &&
	    (tif->tif_flags & (TIFF_STRIPCHOP|TIFF_ISTILED)) == TIFF_STRIPCHOP)
		ChopUpSingleUncompressedStrip(tif);

	/*
	 * Reinitialize i/o since we are starting on a new directory.
	 */
	tif->tif_row = (uint32) -1;
	tif->tif_curstrip = (tstrip_t) -1;
	tif->tif_col = (uint32) -1;
	tif->tif_curtile = (ttile_t) -1;
	tif->tif_tilesize = (tsize_t) -1;

	tif->tif_scanlinesize = TIFFScanlineSize(tif);
	if (!tif->tif_scanlinesize) {
		TIFFErrorExt(tif->tif_clientdata, module,
			     "%s: cannot handle zero scanline size",
			     tif->tif_name);
		return (0);
	}

	if (isTiled(tif)) {
		tif->tif_tilesize = TIFFTileSize(tif);
		if (!tif->tif_tilesize) {
			TIFFErrorExt(tif->tif_clientdata, module,
				     "%s: cannot handle zero tile size",
				     tif->tif_name);
			return (0);
		}
	} else {
		if (!TIFFStripSize(tif)) {
			TIFFErrorExt(tif->tif_clientdata, module,
				     "%s: cannot handle zero strip size",
				     tif->tif_name);
			return (0);
		}
	}
	return (1);
bad:
	if (dir)
		_TIFFfree(dir);
	return (0);
}

/*
 * Read the next TIFF directory from a file and convert it to the internal
 * format. We read directories sequentially.
 */
static void
TIFFReadDirectoryCheckOrder(TIFF* tif, TIFFDirEntryUnion* dir, uint16 dircount)
{
	static const char module[] = "TIFFReadDirectoryCheckOrder";
	int p;
	p=0;
	if (!(tif->tif_flags&TIFF_BIGTIFF))
	{
		uint16 m;
		uint16 n;
		TIFFDirEntryClassic* o;
		m=0;
		for (n=0, o=(TIFFDirEntryClassic*)dir; n<dircount; n++, o++)
		{
			if (o->tdir_tag<m)
			{
				p=1;
				break;
			}
			m=o->tdir_tag+1;
		}
	}
	else
	{
		uint16 m;
		uint16 n;
		TIFFDirEntryBig* o;
		m=0;
		for (n=0, o=(TIFFDirEntryBig*)dir; n<dircount; n++, o++)
		{
			if (o->tdir_tag<m)
			{
				p=1;
				break;
			}
			m=o->tdir_tag+1;
		}
	}
	if (p)
	{
		TIFFWarningExt(tif->tif_clientdata, module,
		    "Invalid TIFF directory; tags are not sorted in ascending order");
	}
}

static TIFFDirEntryUnion*
TIFFReadDirectoryFindEntry(TIFF* tif, TIFFDirEntryUnion* dir, uint16 dircount, uint16 tagid)
{
	if (!(tif->tif_flags&TIFF_BIGTIFF))
	{
		TIFFDirEntryClassic* m;
		uint16 n;
		for (m=dir, n=0; n<dircount; m++, n++)
		{
			if (m->tdir_tag==tagid)
				return((TIFFDirEntryUnion*)m);
		}
	}
	else
	{
		TIFFDirEntryBig* m;
		uint16 n;
		for (m=dir, n=0; n<dircount; m++, n++)
		{
			if (m->tdir_tag==tagid)
				return((TIFFDirEntryUnion*)m);
		}
	}
	return(0);
}

static void
TIFFReadDirectoryFindFieldInfo(TIFF* tif, uint16 tagid, uint16* fii)
{
	int32 ma,mb,mc;
	ma=-1;
	mc=tif->tif_nfields;
	while (1)
	{
		if (ma+1==mc)
		{
			*fii=0xFFFF;
			return;
		}
		mb=(ma+mc)/2;
		if (tif->tif_fieldinfo[mb]->field_tag==tagid)
			break;
		if (tif->tif_fieldinfo[mb]->field_tag<tagid)
			ma=mb;
		else
			mc=mb;
	}
	while (1)
	{
		if (mb==0)
			break;
		if (tif->tif_fieldinfo[mb-1]->field_tag!=tagid)
			break;
		mb--;
	}
	*fii=mb;
}

/*
 * Read custom directory from the arbitarry offset.
 * The code is very similar to TIFFReadDirectory().
 */
int
TIFFReadCustomDirectory(TIFF* tif, toff_t diroff,
			const TIFFFieldInfo info[], size_t n)
{
	static const char module[] = "TIFFReadCustomDirectory";

	assert(0);
	#ifdef NDEF
	TIFFDirectory* td = &tif->tif_dir;
	TIFFDirEntry *dp, *dir = NULL;
	const TIFFFieldInfo* fip;
	size_t fix;
	uint16 i, dircount;

	_TIFFSetupFieldInfo(tif, info, n);

	dircount = TIFFFetchDirectory(tif, diroff, &dir, NULL);
	if (!dircount) {
		TIFFErrorExt(tif->tif_clientdata, module,
			"%s: Failed to read custom directory at offset %lu",
			     tif->tif_name, diroff);
		return 0;
	}

	TIFFFreeDirectory(tif);

	fix = 0;
	for (dp = dir, i = dircount; i > 0; i--, dp++) {

		if (fix >= tif->tif_nfields || dp->tdir_tag == IGNORE)
			continue;

		while (fix < tif->tif_nfields &&
		       tif->tif_fieldinfo[fix]->field_tag < dp->tdir_tag)
			fix++;

		if (fix >= tif->tif_nfields ||
		    tif->tif_fieldinfo[fix]->field_tag != dp->tdir_tag) {

			TIFFWarningExt(tif->tif_clientdata, module,
                        "%s: unknown field with tag %d (0x%x) encountered",
				    tif->tif_name, dp->tdir_tag, dp->tdir_tag,
				    dp->tdir_type);

			TIFFMergeFieldInfo(tif,
					   _TIFFCreateAnonFieldInfo(tif,
						dp->tdir_tag,
						(TIFFDataType)dp->tdir_type),
					   1);

			fix = 0;
			while (fix < tif->tif_nfields &&
			       tif->tif_fieldinfo[fix]->field_tag < dp->tdir_tag)
				fix++;
		}
		/*
		 * Null out old tags that we ignore.
		 */
		if (tif->tif_fieldinfo[fix]->field_bit == FIELD_IGNORE) {
	ignore:
			dp->tdir_tag = IGNORE;
			continue;
		}
		/*
		 * Check data type.
		 */
		fip = tif->tif_fieldinfo[fix];
		while (dp->tdir_type != (unsigned short) fip->field_type
                       && fix < tif->tif_nfields) {
			if (fip->field_type == TIFF_ANY)	/* wildcard */
				break;
                        fip = tif->tif_fieldinfo[++fix];
			if (fix >= tif->tif_nfields ||
			    fip->field_tag != dp->tdir_tag) {
				TIFFWarningExt(tif->tif_clientdata, module,
			"%s: wrong data type %d for \"%s\"; tag ignored",
					    tif->tif_name, dp->tdir_type,
					    tif->tif_fieldinfo[fix-1]->field_name);
				goto ignore;
			}
		}
		/*
		 * Check count if known in advance.
		 */
		if (fip->field_readcount != TIFF_VARIABLE
		    && fip->field_readcount != TIFF_VARIABLE2) {
			uint32 expected = (fip->field_readcount == TIFF_SPP) ?
			    (uint32) td->td_samplesperpixel :
			    (uint32) fip->field_readcount;
			if (!CheckDirCount(tif, dp, expected))
				goto ignore;
		}

		/*
		 * EXIF tags which need to be specifically processed.
		 */
		switch (dp->tdir_tag) {
			case EXIFTAG_SUBJECTDISTANCE:
				(void) TIFFFetchSubjectDistance(tif, dp);
				break;
			default:
				(void) TIFFFetchNormalTag(tif, dp);
				break;
		}
	}

	if (dir)
		_TIFFfree(dir);
	return 1;
	#endif
}

/*
 * EXIF is important special case of custom IFD, so we have a special
 * function to read it.
 */
int
TIFFReadEXIFDirectory(TIFF* tif, toff_t diroff)
{
	size_t exifFieldInfoCount;
	const TIFFFieldInfo *exifFieldInfo =
		_TIFFGetExifFieldInfo(&exifFieldInfoCount);
	return TIFFReadCustomDirectory(tif, diroff, exifFieldInfo,
				       exifFieldInfoCount);
}

static int
EstimateStripByteCounts(TIFF* tif, TIFFDirEntryUnion* dir, uint16 dircount)
{
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		EstimateStripByteCountsClassic(tif,(TIFFDirEntryClassic*)dir,dircount);
	else
		EstimateStripByteCountsBig(tif,(TIFFDirEntryBig*)dir,dircount);
}

static int
EstimateStripByteCountsClassic(TIFF* tif, TIFFDirEntryClassic* dir, uint16 dircount)
{
	static const char module[] = "EstimateStripByteCountsClassic";

	TIFFDirEntryClassic *dp;
	TIFFDirectory *td = &tif->tif_dir;
	uint32 strip;

	if (td->td_stripbytecount)
		_TIFFfree(td->td_stripbytecount);
	td->td_stripbytecount = (uint32*)
	    _TIFFCheckMalloc(tif, td->td_nstrips, sizeof (uint32),
		"for \"StripByteCounts\" array");
	if (td->td_compression != COMPRESSION_NONE) {
		uint32 space = (uint32)(
		    sizeof(TIFFHeaderClassic)
		    + sizeof (uint16)
		    + (dircount * sizeof (TIFFDirEntryClassic))
		    + sizeof (uint32));
		toff_t filesize = TIFFGetFileSize(tif);
		uint16 n;

		/* calculate amount of space used by indirect values */
		for (dp = dir, n = dircount; n > 0; n--, dp++)
		{
			uint32 cc = TIFFDataWidth((TIFFDataType) dp->tdir_type);
			if (cc == 0) {
				TIFFErrorExt(tif->tif_clientdata, module,
			"%s: Cannot determine size of unknown tag type %d",
					  tif->tif_name, dp->tdir_type);
				return -1;
			}
			cc = cc * dp->tdir_count;
			if (cc > sizeof (uint32))
				space += cc;
		}
		space = filesize - space;
		if (td->td_planarconfig == PLANARCONFIG_SEPARATE)
			space /= td->td_samplesperpixel;
		for (strip = 0; strip < td->td_nstrips; strip++)
			td->td_stripbytecount[strip] = space;
		/*
		 * This gross hack handles the case were the offset to
		 * the last strip is past the place where we think the strip
		 * should begin.  Since a strip of data must be contiguous,
		 * it's safe to assume that we've overestimated the amount
		 * of data in the strip and trim this number back accordingly.
		 */ 
		strip--;
		if (((toff_t)(td->td_stripoffset[strip]+
			      td->td_stripbytecount[strip])) > filesize)
			td->td_stripbytecount[strip] =
			    filesize - td->td_stripoffset[strip];
	} else if (isTiled(tif)) {
		uint32 bytespertile = TIFFTileSize(tif);

		for (strip = 0; strip < td->td_nstrips; strip++)
		    td->td_stripbytecount[strip] = bytespertile;
	} else {
		uint32 rowbytes = TIFFScanlineSize(tif);
		uint32 rowsperstrip = td->td_imagelength/td->td_stripsperimage;
		for (strip = 0; strip < td->td_nstrips; strip++)
			td->td_stripbytecount[strip] = rowbytes * rowsperstrip;
	}
	TIFFSetFieldBit(tif, FIELD_STRIPBYTECOUNTS);
	if (!TIFFFieldSet(tif, FIELD_ROWSPERSTRIP))
		td->td_rowsperstrip = td->td_imagelength;
	return 1;
}

static int
EstimateStripByteCountsBig(TIFF* tif, TIFFDirEntryBig* dir, uint16 dircount)
{
	static const char module[] = "EstimateStripByteCountsBig";

	TIFFDirEntryBig *dp;
	TIFFDirectory *td = &tif->tif_dir;
	uint32 strip;

	if (td->td_stripbytecount)
		_TIFFfree(td->td_stripbytecount);
	td->td_stripbytecount = (uint32*)
	    _TIFFCheckMalloc(tif, td->td_nstrips, sizeof (uint32),
		"for \"StripByteCounts\" array");
	if (td->td_compression != COMPRESSION_NONE) {
		uint32 space = (uint32)(
		    sizeof(TIFFHeaderBig)
		    + sizeof (uint64)
		    + (dircount * sizeof (TIFFDirEntryBig))
		    + sizeof (uint64));
		toff_t filesize = TIFFGetFileSize(tif);
		uint16 n;

		/* calculate amount of space used by indirect values */
		for (dp = dir, n = dircount; n > 0; n--, dp++)
		{
			uint32 cc = TIFFDataWidth((TIFFDataType) dp->tdir_type);
			if (cc == 0) {
				TIFFErrorExt(tif->tif_clientdata, module,
				    "%s: Cannot determine size of unknown tag type %d",
				    tif->tif_name, dp->tdir_type);
				return -1;
			}
			cc = cc * dp->tdir_count;
			if (cc > sizeof (uint64))
				space += cc;
		}
		space = filesize - space;
		if (td->td_planarconfig == PLANARCONFIG_SEPARATE)
			space /= td->td_samplesperpixel;
		for (strip = 0; strip < td->td_nstrips; strip++)
			td->td_stripbytecount[strip] = space;
		/*
		 * This gross hack handles the case were the offset to
		 * the last strip is past the place where we think the strip
		 * should begin.  Since a strip of data must be contiguous,
		 * it's safe to assume that we've overestimated the amount
		 * of data in the strip and trim this number back accordingly.
		 */
		strip--;
		if (((toff_t)(td->td_stripoffset[strip]+
			      td->td_stripbytecount[strip])) > filesize)
			td->td_stripbytecount[strip] =
			    filesize - td->td_stripoffset[strip];
	} else if (isTiled(tif)) {
		uint32 bytespertile = TIFFTileSize(tif);

		for (strip = 0; strip < td->td_nstrips; strip++)
		    td->td_stripbytecount[strip] = bytespertile;
	} else {
		uint32 rowbytes = TIFFScanlineSize(tif);
		uint32 rowsperstrip = td->td_imagelength/td->td_stripsperimage;
		for (strip = 0; strip < td->td_nstrips; strip++)
			td->td_stripbytecount[strip] = rowbytes * rowsperstrip;
	}
	TIFFSetFieldBit(tif, FIELD_STRIPBYTECOUNTS);
	if (!TIFFFieldSet(tif, FIELD_ROWSPERSTRIP))
		td->td_rowsperstrip = td->td_imagelength;
	return 1;
}

static void
MissingRequired(TIFF* tif, const char* tagname)
{
	static const char module[] = "MissingRequired";

	TIFFErrorExt(tif->tif_clientdata, module,
		  "%s: TIFF directory is missing required \"%s\" field",
		  tif->tif_name, tagname);
}

/*
 * Check the directory offset against the list of already seen directory
 * offsets. This is a trick to prevent IFD looping. The one can create TIFF
 * file with looped directory pointers. We will maintain a list of already
 * seen directories and check every IFD offset against that list.
 */
static int
TIFFCheckDirOffset(TIFF* tif, uint64 diroff)
{
	uint16 n;

	if (diroff == 0)			/* no more directories */
		return 0;

	for (n = 0; n < tif->tif_dirnumber && tif->tif_dirlist; n++) {
		if (tif->tif_dirlist[n] == diroff)
			return 0;
	}

	tif->tif_dirnumber++;

	if (tif->tif_dirnumber > tif->tif_dirlistsize) {
		uint64* new_dirlist;

		/*
		 * XXX: Reduce memory allocation granularity of the dirlist
		 * array.
		 */
		new_dirlist = (uint64*)_TIFFCheckRealloc(tif,
							  tif->tif_dirlist,
							  tif->tif_dirnumber,
							  2 * sizeof(uint64),
							  "for IFD list");
		if (!new_dirlist)
			return 0;
		tif->tif_dirlistsize = 2 * tif->tif_dirnumber;
		tif->tif_dirlist = new_dirlist;
	}

	tif->tif_dirlist[tif->tif_dirnumber - 1] = diroff;

	return 1;
}

/*
 * Check the count field of a directory entry against a known value.  The
 * caller is expected to skip/ignore the tag if there is a mismatch.
 */
static int
CheckDirCount(TIFF* tif, TIFFDirEntryUnion* dir, uint32 count)
{
	uint64 dircount;
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		dircount=dir->classic.tdir_count;
	else
		dircount=dir->big.tdir_count;
	if (count > dircount) {
		TIFFWarningExt(tif->tif_clientdata, tif->tif_name,
		    "incorrect count for field \"%s\" (%llu, expecting %lu); tag ignored",
		    _TIFFFieldWithTag(tif, dir->common.tdir_tag)->field_name,
		    dircount, count);
		return (0);
	} else if (count < dircount) {
		TIFFWarningExt(tif->tif_clientdata, tif->tif_name,
		    "incorrect count for field \"%s\" (%llu, expecting %lu); tag trimmed",
		    _TIFFFieldWithTag(tif, dir->common.tdir_tag)->field_name,
		    dircount, count);
		return (1);
	}
	return (1);
}

/*
 * Read IFD structure from the specified offset. If the pointer to
 * nextdiroff variable has been specified, read it too. Function returns a
 * number of fields in the directory or 0 if failed.
 */
static uint16
TIFFFetchDirectory(TIFF* tif, uint64 diroff, TIFFDirEntryUnion** pdir,
    uint64 *nextdiroff)
{
	static const char module[] = "TIFFFetchDirectoryClassic";

	TIFFDirEntryUnion *dir;
	uint64 dircount64;
	uint16 dircount16;
	uint32 dirsize;

	assert(pdir);

	tif->tif_diroff = diroff;
	if (nextdiroff)
		*nextdiroff = 0;
	if (!isMapped(tif)) {
		if (!SeekOK(tif, tif->tif_diroff)) {
			TIFFErrorExt(tif->tif_clientdata, module,
				"%s: Seek error accessing TIFF directory",
				tif->tif_name);
			return 0;
		}
		if (!(tif->tif_flags&TIFF_BIGTIFF))
		{
			if (!ReadOK(tif, &dircount16, sizeof (uint16))) {
				TIFFErrorExt(tif->tif_clientdata, module,
				    "%s: Can not read TIFF directory count",
				    tif->tif_name);
				return 0;
			}
			if (tif->tif_flags & TIFF_SWAB)
				TIFFSwabShort(&dircount16);
			if (dircount16>4096)
			{
				TIFFErrorExt(tif->tif_clientdata, module,
				    "Sanity check on directory count failed, this is probably not a valid IFD offset");
				return 0;
			}
			dirsize = sizeof(TIFFDirEntryClassic);
		} else {
			if (!ReadOK(tif, &dircount64, sizeof (uint64))) {
				TIFFErrorExt(tif->tif_clientdata, module,
					"%s: Can not read TIFF directory count",
					tif->tif_name);
				return 0;
			}
			if (tif->tif_flags & TIFF_SWAB)
				TIFFSwabLong8(&dircount64);
			if (dircount64>4096)
			{
				TIFFErrorExt(tif->tif_clientdata, module,
				    "Sanity check on directory count failed, this is probably not a valid IFD offset");
				return 0;
			}
			dircount16 = (uint16)dircount64;
			dirsize = sizeof(TIFFDirEntryBig);
		}
		dir = (TIFFDirEntryUnion *)_TIFFCheckMalloc(tif, dircount16,
		    dirsize, "to read TIFF directory");
		if (dir == NULL)
			return 0;
		if (!ReadOK(tif, dir, dircount16*dirsize)) {
			TIFFErrorExt(tif->tif_clientdata, module,
				"%.100s: Can not read TIFF directory",
				tif->tif_name);
			_TIFFfree(dir);
			return 0;
		}
		/*
		 * Read offset to next directory for sequential scans if
		 * needed.
		 */
		if (nextdiroff)
		{
			if (!(tif->tif_flags&TIFF_BIGTIFF))
			{
				uint32 nextdiroff32;
				if (!ReadOK(tif, &nextdiroff32, sizeof(uint32)))
					nextdiroff32 = 0;
				if (tif->tif_flags&TIFF_SWAB)
					TIFFSwabLong(&nextdiroff32);
				*nextdiroff=nextdiroff32;
			} else {
				if (!ReadOK(tif, nextdiroff, sizeof(uint64)))
					*nextdiroff = 0;
				if (tif->tif_flags&TIFF_SWAB)
					TIFFSwabLong8(nextdiroff);
			}
		}
	} else {
		toff_t off = tif->tif_diroff;

		/*
		 * Check for integer overflow when validating the dir_off,
		 * otherwise a very high offset may cause an OOB read and
		 * crash the client. Make two comparisons instead of
		 *
		 *  off + sizeof(uint16) > tif->tif_size
		 *
		 * to avoid overflow.
		 */
		if (!(tif->tif_flags&TIFF_BIGTIFF))
		{
			if (tif->tif_size < sizeof (uint16) ||
			    off > tif->tif_size - sizeof(uint16)) {
				TIFFErrorExt(tif->tif_clientdata, module,
					"%s: Can not read TIFF directory count",
					tif->tif_name);
				return 0;
			} else {
				_TIFFmemcpy(&dircount16, tif->tif_base + off,
					    sizeof(uint16));
			}
			off += sizeof (uint16);
			if (tif->tif_flags & TIFF_SWAB)
				TIFFSwabShort(&dircount16);
			if (dircount16>4096)
			{
				TIFFErrorExt(tif->tif_clientdata, module,
				    "Sanity check on directory count failed, this is probably not a valid IFD offset");
				return 0;
			}
			dirsize = sizeof(TIFFDirEntryClassic);
		}
		else
		{
			if (tif->tif_size < sizeof (uint64) ||
			    off > tif->tif_size - sizeof(uint64)) {
				TIFFErrorExt(tif->tif_clientdata, module,
					"%s: Can not read TIFF directory count",
					tif->tif_name);
				return 0;
			} else {
				_TIFFmemcpy(&dircount64, tif->tif_base + off,
					    sizeof(uint64));
			}
			off += sizeof (uint64);
			if (tif->tif_flags & TIFF_SWAB)
				TIFFSwabLong8(&dircount64);
			if (dircount64>4096)
			{
				TIFFErrorExt(tif->tif_clientdata, module,
				    "Sanity check on directory count failed, this is probably not a valid IFD offset");
				return 0;
			}
			dircount16 = (uint16)dircount64;
			dirsize = sizeof(TIFFDirEntryBig);
		}
		dir = (TIFFDirEntryUnion *)_TIFFCheckMalloc(tif, dircount16,
						dirsize,
						"to read TIFF directory");
		if (dir == NULL)
			return 0;
		if (off + dircount16 * dirsize > tif->tif_size) {
			TIFFErrorExt(tif->tif_clientdata, module,
				     "%s: Can not read TIFF directory",
				     tif->tif_name);
			_TIFFfree(dir);
			return 0;
		} else {
			_TIFFmemcpy(dir, tif->tif_base + off,
				    dircount16 * dirsize);
		}
		if (nextdiroff) {
			off += dircount16 * dirsize;
			if (!(tif->tif_flags&TIFF_BIGTIFF))
			{
				uint32 nextdiroff32;
				if (off + sizeof (uint32) <= tif->tif_size) {
					_TIFFmemcpy(&nextdiroff32, tif->tif_base + off,
						    sizeof (uint32));
				}
				else
					nextdiroff32 = 0;
				if (tif->tif_flags&TIFF_SWAB)
					TIFFSwabLong(&nextdiroff32);
				*nextdiroff = nextdiroff32;
			}
			else
			{
				if (off + sizeof (uint64) <= tif->tif_size) {
					_TIFFmemcpy(nextdiroff, tif->tif_base + off,
						    sizeof (uint64));
				}
				else
					*nextdiroff = 0;
				if (tif->tif_flags&TIFF_SWAB)
					TIFFSwabLong8(nextdiroff);
			}
		}
	}
	if (tif->tif_flags&TIFF_SWAB)
	{
		if (!(tif->tif_flags&TIFF_BIGTIFF))
		{
			TIFFDirEntryClassic* m;
			uint16 n;
			m = (TIFFDirEntryClassic*) dir;
			for (n=0; n<dircount16; n++)
			{
				TIFFSwabShort(&m->tdir_tag);
				TIFFSwabShort(&m->tdir_type);
				TIFFSwabLong(&m->tdir_count);
				m++;
			}
		}
		else
		{
			TIFFDirEntryBig* m;
			uint16 n;
			m = (TIFFDirEntryBig*) dir;
			for (n=0; n<dircount16; n++)
			{
				TIFFSwabShort(&m->tdir_tag);
				TIFFSwabShort(&m->tdir_type);
				TIFFSwabLong8(&m->tdir_count);
				m++;
			}
		}
	}
	*pdir = dir;
	return dircount16;
}

/*
 * Fetch a contiguous directory item.
 */
static uint32
TIFFFetchData(TIFF* tif, TIFFDirEntryUnion* dir, char* cp)
{
	uint32 width;
	uint64 count;
	uint64 offset;
	uint64 size;
	width = TIFFDataWidth((TIFFDataType) dir->common.tdir_type);
	if (width==0)
		goto bad;
	if (!(tif->tif_flags&TIFF_BIGTIFF))
	{
		count = dir->classic.tdir_count;
		offset = dir->classic.tdir_offset.vu32;
	}
	else
	{
		count = dir->big.tdir_count;
		offset = dir->big.tdir_offset.vu64;
	}
	if (count==0)
		goto bad;  /* is legit, though, should not really return an error */
	size = count*width;
	if (size/width!=count)
		goto bad;  /* overflow */

	if (!isMapped(tif)) {
		if (!SeekOK(tif, offset))
			goto bad;
		if (!ReadOK(tif, cp, size))
			goto bad;
	} else {
		if ((offset+size<offset)||
		    (offset+size<size)||
		    (offset+size>tif->tif_size))
			goto bad;     /* overflow */
		_TIFFmemcpy(cp, tif->tif_base + offset, size);
	}
	if (tif->tif_flags & TIFF_SWAB) {
		switch (dir->common.tdir_type) {
		case TIFF_SHORT:
		case TIFF_SSHORT:
			TIFFSwabArrayOfShort((uint16*) cp, count);
			break;
		case TIFF_LONG:
		case TIFF_SLONG:
		case TIFF_FLOAT:
		case TIFF_IFD:
			TIFFSwabArrayOfLong((uint32*) cp, count);
			break;
		case TIFF_RATIONAL:
		case TIFF_SRATIONAL:
			TIFFSwabArrayOfLong((uint32*) cp, 2*count);
			break;
		case TIFF_DOUBLE:
			TIFFSwabArrayOfDouble((double*) cp, count);
			break;
		case TIFF_LONG8:
		case TIFF_SLONG8:
		case TIFF_IFD8:
			TIFFSwabArrayOfLong8((uint64*) cp, count);
		}
	}
	return (size);
bad:
	TIFFErrorExt(tif->tif_clientdata, tif->tif_name,
		     "Error fetching data for field \"%s\"",
		     _TIFFFieldWithTag(tif, dir->common.tdir_tag)->field_name);
	return 0;
}

/*
 * Fetch an ASCII item from the file.
 */
static uint32
TIFFFetchString(TIFF* tif, TIFFDirEntryUnion* dir, char* cp)
{
	if (!(tif->tif_flags&TIFF_BIGTIFF))
	{
		if (dir->classic.tdir_count <= 4) {
			uint32 l = dir->classic.tdir_offset.vu32;
			if (tif->tif_flags & TIFF_SWAB)
				TIFFSwabLong(&l);
			_TIFFmemcpy(cp, &l, dir->classic.tdir_count);
			return (1);
		}
	}
	else
	{
		if (dir->big.tdir_count <= 8) {
			uint64 l = dir->big.tdir_offset.vu64;
			if (tif->tif_flags & TIFF_SWAB)
				TIFFSwabLong8(&l);
			_TIFFmemcpy(cp, &l, dir->big.tdir_count);
			return (1);
		}
	}
	return (TIFFFetchData(tif, dir, cp));
}

/*
 * Convert numerator+denominator to float.
 */
static int
cvtRational(TIFF* tif, TIFFDirEntryUnion* dir, uint32 num, uint32 denom, float* rv)
{
	if (denom == 0) {
		TIFFErrorExt(tif->tif_clientdata, tif->tif_name,
		    "%s: Rational with zero denominator (num = %lu)",
		    _TIFFFieldWithTag(tif, dir->common.tdir_tag)->field_name, num);
		return (0);
	} else {
		if (dir->common.tdir_type == TIFF_RATIONAL)
			*rv = ((float)num / (float)denom);
		else
			*rv = ((float)(int32)num / (float)(int32)denom);
		return (1);
	}
}

/*
 * Fetch a rational item from the file at offset off and return the value as a
 * floating point number.
 */
static float
TIFFFetchRational(TIFF* tif, TIFFDirEntryUnion* dir)
{
	uint32 l[2];
	float v;

	return (!TIFFFetchData(tif, dir, (char *)l) ||
	    !cvtRational(tif, dir, l[0], l[1], &v) ? 1.0f : v);
}

/*
 * Fetch a single floating point value from the offset field and return it as
 * a native float.
 */
static float
TIFFFetchFloat(TIFF* tif, TIFFDirEntryUnion* dir)
{

	assert(0);
	#ifdef NDEF
	float v;
	int32 l = TIFFExtractData(tif, dir->tdir_type, dir->tdir_offset);
	_TIFFmemcpy(&v, &l, sizeof(float));
	TIFFCvtIEEEFloatToNative(tif, 1, &v);
	return (v);
	#endif
}

/*
 * Fetch an array of BYTE or SBYTE values.
 */
static int
TIFFFetchByteArray(TIFF* tif, TIFFDirEntryUnion* dir, uint8* v)
{
	assert(0);
	#ifdef NDEF
    if (dir->tdir_count <= 4) {
	/*
	 * Extract data from offset field.
	 */
	if (tif->tif_header.common.tiff_magic == TIFF_BIGENDIAN) {
	    if (dir->tdir_type == TIFF_SBYTE)
		switch (dir->tdir_count) {
		    case 4: v[3] = dir->tdir_offset & 0xff;
		    case 3: v[2] = (dir->tdir_offset >> 8) & 0xff;
		    case 2: v[1] = (dir->tdir_offset >> 16) & 0xff;
		    case 1: v[0] = dir->tdir_offset >> 24;
		}
	    else
		switch (dir->tdir_count) {
		    case 4: v[3] = dir->tdir_offset & 0xff;
		    case 3: v[2] = (dir->tdir_offset >> 8) & 0xff;
		    case 2: v[1] = (dir->tdir_offset >> 16) & 0xff;
		    case 1: v[0] = dir->tdir_offset >> 24;
		}
	} else {
	    if (dir->tdir_type == TIFF_SBYTE)
		switch (dir->tdir_count) {
		    case 4: v[3] = dir->tdir_offset >> 24;
		    case 3: v[2] = (dir->tdir_offset >> 16) & 0xff;
		    case 2: v[1] = (dir->tdir_offset >> 8) & 0xff;
		    case 1: v[0] = dir->tdir_offset & 0xff;
		}
	    else
		switch (dir->tdir_count) {
		    case 4: v[3] = dir->tdir_offset >> 24;
		    case 3: v[2] = (dir->tdir_offset >> 16) & 0xff;
		    case 2: v[1] = (dir->tdir_offset >> 8) & 0xff;
		    case 1: v[0] = dir->tdir_offset & 0xff;
		}
	}
	return (1);
    } else
	return (TIFFFetchData(tif, dir, (char*) v) != 0);	/* XXX */
	#endif
}

/*
 * Fetch an array of SHORT or SSHORT values.
 */
static int
TIFFFetchShortArray(TIFF* tif, TIFFDirEntryUnion* dir, uint16* v)
{
	assert(0);
	#ifdef NDEF
	if (!(tif->tif_flags&TIFF_BIGTIFF))
	{
		if (dir->classic.tdir_count <= 2) {
			if (tif->tif_header.classic.tiff_magic == TIFF_BIGENDIAN) {
				switch (dir->classic.tdir_count) {
					case 2: v[1] = (uint16) (dir->classic.tdir_offset & 0xffff);
					case 1: v[0] = (uint16) (dir->classic.tdir_offset >> 16);
				}
			} else {
				switch (dir->classic.tdir_count) {
					case 2: v[1] = (uint16) (dir->classic.tdir_offset >> 16);
					case 1: v[0] = (uint16) (dir->classic.tdir_offset & 0xffff);
				}
			}
			return (1);
		}
	} else {
		if (dir->big.tdir_count <= 4) {
			if (tif->tif_header.big.tiff_magic == TIFF_BIGENDIAN) {
				switch (dir->big.tdir_count) {
					case 3: v[3] = (uint16) (dir->big.tdir_offset & 0xffff);
					case 4: v[2] = (uint16) ((dir->big.tdir_offset >> 16) & 0xffff);
					case 2: v[1] = (uint16) ((dir->big.tdir_offset >> 32) & 0xffff);
					case 1: v[0] = (uint16) ((dir->big.tdir_offset >> 48) & 0xffff);
				}
			} else {
				switch (dir->big.tdir_count) {
					case 4: v[3] = (uint16) ((dir->big.tdir_offset >> 48) & 0xffff);
					case 3: v[2] = (uint16) ((dir->big.tdir_offset >> 32) & 0xffff);
					case 2: v[1] = (uint16) ((dir->big.tdir_offset >> 16) & 0xffff);
					case 1: v[0] = (uint16) (dir->big.tdir_offset & 0xffff);
				}
			}
			return (1);
		}
	}
	return (TIFFFetchData(tif, dir, (char *)v) != 0);
	#endif
}

/*
 * Fetch a pair of SHORT or BYTE values. Some tags may have either BYTE
 * or SHORT type and this function works with both ones.
 */
static int
TIFFFetchShortPair(TIFF* tif, TIFFDirEntryUnion* dir)
{
	assert(0);
	#ifdef NDEF

	/*
	 * Prevent overflowing the v stack arrays below by performing a sanity
	 * check on tdir_count, this should never be greater than two.
	 */
	if (dir->tdir_count > 2) {
		TIFFWarningExt(tif->tif_clientdata, tif->tif_name,
		"unexpected count for field \"%s\", %lu, expected 2; ignored",
			_TIFFFieldWithTag(tif, dir->tdir_tag)->field_name,
			dir->tdir_count);
		return 0;
	}

	switch (dir->tdir_type) {
		case TIFF_BYTE:
		case TIFF_SBYTE:
			{
			uint8 v[4];
			return TIFFFetchByteArray(tif, dir, v)
				&& TIFFSetField(tif, dir->tdir_tag, v[0], v[1]);
			}
		case TIFF_SHORT:
		case TIFF_SSHORT:
			{
			uint16 v[2];
			return TIFFFetchShortArray(tif, dir, v)
				&& TIFFSetField(tif, dir->tdir_tag, v[0], v[1]);
			}
		default:
			return 0;
	}
	#endif
}

/*
 * Fetch an array of LONG or SLONG values.
 */
static int
TIFFFetchLongArray(TIFF* tif, TIFFDirEntryUnion* dir, uint32* v)
{
	#ifdef NDEF
	if (dir->tdir_count == 1) {
		v[0] = dir->tdir_offset;
		return (1);
	} else
		return (TIFFFetchData(tif, dir, (char*) v) != 0);
	#endif
}

/*
 * Fetch an array of RATIONAL or SRATIONAL values.
 */
static int
TIFFFetchRationalArray(TIFF* tif, TIFFDirEntryUnion* dir, float* v)
{
	assert(0);
	#ifdef NDEF
	int ok = 0;
	uint32* l;

	l = (uint32*)_TIFFCheckMalloc(tif,
	    dir->tdir_count, TIFFDataWidth((TIFFDataType) dir->tdir_type),
	    "to fetch array of rationals");
	if (l) {
		if (TIFFFetchData(tif, dir, (char *)l)) {
			uint32 i;
			for (i = 0; i < dir->tdir_count; i++) {
				ok = cvtRational(tif, dir,
				    l[2*i+0], l[2*i+1], &v[i]);
				if (!ok)
					break;
			}
		}
		_TIFFfree((char *)l);
	}
	return (ok);
	#endif
}

/*
 * Fetch an array of FLOAT values.
 */
static int
TIFFFetchFloatArray(TIFF* tif, TIFFDirEntryUnion* dir, float* v)
{
	assert(0);
	#ifdef NDEF

	if (dir->tdir_count == 1) {
		v[0] = *(float*) &dir->tdir_offset;
		TIFFCvtIEEEFloatToNative(tif, dir->tdir_count, v);
		return (1);
	} else	if (TIFFFetchData(tif, dir, (char*) v)) {
		TIFFCvtIEEEFloatToNative(tif, dir->tdir_count, v);
		return (1);
	} else
		return (0);
	#endif
}

/*
 * Fetch an array of DOUBLE values.
 */
static int
TIFFFetchDoubleArray(TIFF* tif, TIFFDirEntryUnion* dir, double* v)
{
	assert(0);
	#ifdef NDEF

	if (TIFFFetchData(tif, dir, (char*) v)) {
		TIFFCvtIEEEDoubleToNative(tif, dir->tdir_count, v);
		return (1);
	} else
		return (0);
	#endif
}

/*
 * Fetch an array of ANY values.  The actual values are returned as doubles
 * which should be able hold all the types.  Yes, there really should be an
 * tany_t to avoid this potential non-portability ...  Note in particular that
 * we assume that the double return value vector is large enough to read in
 * any fundamental type.  We use that vector as a buffer to read in the base
 * type vector and then convert it in place to double (from end to front of
 * course).
 */
static int
TIFFFetchAnyArray(TIFF* tif, TIFFDirEntryUnion* dir, double* v)
{
	assert(0);
	#ifdef NDEF

	int i;

	switch (dir->tdir_type) {
	case TIFF_BYTE:
	case TIFF_SBYTE:
		if (!TIFFFetchByteArray(tif, dir, (uint8*) v))
			return (0);
		if (dir->tdir_type == TIFF_BYTE) {
			uint8* vp = (uint8*) v;
			for (i = dir->tdir_count-1; i >= 0; i--)
				v[i] = vp[i];
		} else {
			int8* vp = (int8*) v;
			for (i = dir->tdir_count-1; i >= 0; i--)
				v[i] = vp[i];
		}
		break;
	case TIFF_SHORT:
	case TIFF_SSHORT:
		if (!TIFFFetchShortArray(tif, dir, (uint16*) v))
			return (0);
		if (dir->tdir_type == TIFF_SHORT) {
			uint16* vp = (uint16*) v;
			for (i = dir->tdir_count-1; i >= 0; i--)
				v[i] = vp[i];
		} else {
			int16* vp = (int16*) v;
			for (i = dir->tdir_count-1; i >= 0; i--)
				v[i] = vp[i];
		}
		break;
	case TIFF_LONG:
	case TIFF_SLONG:
		if (!TIFFFetchLongArray(tif, dir, (uint32*) v))
			return (0);
		if (dir->tdir_type == TIFF_LONG) {
			uint32* vp = (uint32*) v;
			for (i = dir->tdir_count-1; i >= 0; i--)
				v[i] = vp[i];
		} else {
			int32* vp = (int32*) v;
			for (i = dir->tdir_count-1; i >= 0; i--)
				v[i] = vp[i];
		}
		break;
	case TIFF_RATIONAL:
	case TIFF_SRATIONAL:
		if (!TIFFFetchRationalArray(tif, dir, (float*) v))
			return (0);
		{ float* vp = (float*) v;
		  for (i = dir->tdir_count-1; i >= 0; i--)
			v[i] = vp[i];
		}
		break;
	case TIFF_FLOAT:
		if (!TIFFFetchFloatArray(tif, dir, (float*) v))
			return (0);
		{ float* vp = (float*) v;
		  for (i = dir->tdir_count-1; i >= 0; i--)
			v[i] = vp[i];
		}
		break;
	case TIFF_DOUBLE:
		return (TIFFFetchDoubleArray(tif, dir, (double*) v));
	default:
		/* TIFF_NOTYPE */
		/* TIFF_ASCII */
		/* TIFF_UNDEFINED */
		TIFFErrorExt(tif->tif_clientdata, tif->tif_name,
			     "cannot read TIFF_ANY type %d for field \"%s\"",
			     dir->tdir_type,
			     _TIFFFieldWithTag(tif, dir->tdir_tag)->field_name);
		return (0);
	}
	return (1);
	#endif
}

/*
 * Fetch a tag that is not handled by special case code.
 */
static int
TIFFFetchNormalTag(TIFF* tif, TIFFDirEntryUnion* dp)
{
	static const char mesg[] = "to fetch tag value";
	int ok = 0;
	const TIFFFieldInfo* fip = _TIFFFieldWithTag(tif, dp->common.tdir_tag);
	uint64 count;

	if (!(tif->tif_flags&TIFF_BIGTIFF))
		count = dp->classic.tdir_count;
	else
		count = dp->big.tdir_count;
	if (count > 1) {
		/* array of values */
		enum TIFFReadDirEntryErr err;
		void* data;
		switch (dp->common.tdir_type) {
			case TIFF_BYTE:
			case TIFF_SBYTE:
				assert(0);
				#ifdef NDEF
				cp = (char *)_TIFFCheckMalloc(tif,
				    count, sizeof (uint8), mesg);
				ok = cp && TIFFFetchByteArray(tif, dp, (uint8*) cp);
				break;
				#endif
			case TIFF_SHORT:
				err=TIFFReadDirEntryShortArray(tif,dp,(uint16**)(&data));
				break;
			case TIFF_SSHORT:
				assert(0);
				#ifdef NDEF
				cp = (char *)_TIFFCheckMalloc(tif,
				    count, sizeof (uint16), mesg);
				ok = cp && TIFFFetchShortArray(tif, dp, (uint16*) cp);
				break;
				#endif
			case TIFF_LONG:
			case TIFF_SLONG:
				assert(0);
				#ifdef NDEF
				cp = (char *)_TIFFCheckMalloc(tif,
				    count, sizeof (uint32), mesg);
				ok = cp && TIFFFetchLongArray(tif, dp, (uint32*) cp);
				break;
				#endif
			case TIFF_RATIONAL:
			case TIFF_SRATIONAL:
				assert(0);
				#ifdef NDEF
				cp = (char *)_TIFFCheckMalloc(tif,
				    count, sizeof (float), mesg);
				ok = cp && TIFFFetchRationalArray(tif, dp, (float*) cp);
				break;
				#endif
			case TIFF_FLOAT:
				assert(0);
				#ifdef NDEF
				cp = (char *)_TIFFCheckMalloc(tif,
				    count, sizeof (float), mesg);
				ok = cp && TIFFFetchFloatArray(tif, dp, (float*) cp);
				break;
				#endif
			case TIFF_DOUBLE:
				assert(0);
				#ifdef NDEF
				cp = (char *)_TIFFCheckMalloc(tif,
				    count, sizeof (double), mesg);
				ok = cp && TIFFFetchDoubleArray(tif, dp, (double*) cp);
				break;
				#endif
			case TIFF_ASCII:
				err=TIFFReadDirEntryByteArray(tif,dp,(uint8**)(&data));
				if ((err==TIFFReadDirEntryErrOk)&&(fip->field_passcount==0)&&((count==0)||(((uint8*)data)[(uint32)count-1]!=0)))
				{
					assert(0);
				}
				break;
			case TIFF_UNDEFINED:
				err=TIFFReadDirEntryByteArray(tif,dp,(uint8**)(&data));
				break;
		}
		if (err!=TIFFReadDirEntryErrOk)
			ok=1;
		else
		{
			if (fip->field_passcount)
				ok=TIFFSetField(tif,dp->common.tdir_tag,count,data);
			else
				ok=TIFFSetField(tif,dp->common.tdir_tag,data);
			_TIFFfree(data);
		}
	} else {
		switch (dp->common.tdir_type) {
			case TIFF_BYTE:
			case TIFF_SBYTE:
			case TIFF_SHORT:
			case TIFF_SSHORT:
				/*
				 * If the tag is also acceptable as a LONG or SLONG
				 * then TIFFSetField will expect an uint32 parameter
				 * passed to it (through varargs).  Thus, for machines
				 * where sizeof (int) != sizeof (uint32) we must do
				 * a careful check here.  It's hard to say if this
				 * is worth optimizing.
				 *
				 * NB: We use TIFFFieldWithTag here knowing that
				 *     it returns us the first entry in the table
				 *     for the tag and that that entry is for the
				 *     widest potential data type the tag may have.
				 */
				{
					TIFFDataType type = fip->field_type;
					if (type != TIFF_LONG && type != TIFF_SLONG) {
						enum TIFFReadDirEntryErr err;
						uint16 v;
						err=TIFFReadDirEntryShort(tif,dp,&v);
						assert(err==TIFFReadDirEntryErrOk);
						ok = (fip->field_passcount ?
						    TIFFSetField(tif, dp->common.tdir_tag, 1, &v)
						    : TIFFSetField(tif, dp->common.tdir_tag, v));
						break;
					}
				}
				/* fall thru... */
			case TIFF_LONG:
			case TIFF_SLONG:
				{
					enum TIFFReadDirEntryErr err;
					uint32 v;
					err=TIFFReadDirEntryLong(tif,dp,&v);
					assert(err==TIFFReadDirEntryErrOk);
					ok = (fip->field_passcount ?
					    TIFFSetField(tif, dp->common.tdir_tag, 1, &v)
					    : TIFFSetField(tif, dp->common.tdir_tag, v));
				}
				break;
			case TIFF_RATIONAL:
			case TIFF_SRATIONAL:
			case TIFF_FLOAT:
				assert(0);
				#ifdef NDEF
				{
					float v = (dp->common.tdir_type == TIFF_FLOAT ?
					    TIFFFetchFloat(tif, dp)
					    : TIFFFetchRational(tif, dp));
					ok = (fip->field_passcount ?
					    TIFFSetField(tif, dp->common.tdir_tag, 1, &v)
					    : TIFFSetField(tif, dp->common.tdir_tag, v));
				}
				break;
				#endif
			case TIFF_DOUBLE:
				assert(0);
				#ifdef NDEF
				{
					double v;
					ok = (TIFFFetchDoubleArray(tif, dp, &v) &&
					    (fip->field_passcount ?
					    TIFFSetField(tif, dp->common.tdir_tag, 1, &v)
					    : TIFFSetField(tif, dp->common.tdir_tag, v))
					    );
				}
				break;
				#endif
			case TIFF_ASCII:
			case TIFF_UNDEFINED:        /* bit of a cheat... */
				assert(0);
				#ifdef NDEF
				{
					char c[2];
					if ( (ok = (TIFFFetchString(tif, dp, c) != 0)) != 0 ) {
						c[1] = '\0';        /* XXX paranoid */
						ok = (fip->field_passcount ?
						    TIFFSetField(tif, dp->common.tdir_tag, 1, c)
						    : TIFFSetField(tif, dp->common.tdir_tag, c));
					}
				}
				break;
				#endif
		}
	}
	return (ok);
}

#define NITEMS(x) (sizeof (x) / sizeof (x[0]))
/*
 * Fetch samples/pixel short values for 
 * the specified tag and verify that
 * all values are the same.
 */
static int
TIFFFetchPerSampleShorts(TIFF* tif, TIFFDirEntryUnion* dir, uint16* pl)
{
	uint16 samples = tif->tif_dir.td_samplesperpixel;
	int status = 0;
	if (CheckDirCount(tif, dir, (uint32) samples)) {
		uint16 buf[10];
		uint16* v = buf;
		uint64 count;
		if (!(tif->tif_flags&TIFF_BIGTIFF))
			count = dir->classic.tdir_count;
		else
			count = dir->big.tdir_count;
		if (count > NITEMS(buf))
			v = (uint16*) _TIFFCheckMalloc(tif, count, sizeof(uint16),
			    "to fetch per-sample values");
		if (v && TIFFFetchShortArray(tif, dir, v)) {
			uint16 i;
			uint16 check_count;
			if ((uint64)samples<=count)
				check_count=samples;
			else
				check_count=(uint16)count;
			for (i=1; i<check_count; i++)
				if (v[i] != v[0]) {
					TIFFErrorExt(tif->tif_clientdata, tif->tif_name,
					    "Cannot handle different per-sample values for field \"%s\"",
					    _TIFFFieldWithTag(tif, dir->common.tdir_tag)->field_name);
					goto bad;
				}
			*pl = v[0];
			status = 1;
		}
	bad:
		if (v && v != buf)
			_TIFFfree(v);
	}
	return (status);
}

/*
 * Fetch samples/pixel long values for
 * the specified tag and verify that
 * all values are the same.
 */
static int
TIFFFetchPerSampleLongs(TIFF* tif, TIFFDirEntryUnion* dir, uint32* pl)
{
	assert(0);
	#ifdef NDEF
	uint16 samples = tif->tif_dir.td_samplesperpixel;
	int status = 0;
	if (CheckDirCount(tif, dir, (uint32) samples)) {
		uint32 buf[10];
		uint32* v = buf;
		uint64 count;
		if (!(tif->tif_flags&TIFF_BIGTIFF))
			count = dir->classic.tdir_count;
		else
			count = dir->big.tdir_count;
		if (count > NITEMS(buf))
			v = (uint32*) _TIFFCheckMalloc(tif, count, sizeof(uint32),
			    "to fetch per-sample values");
		if (v && TIFFFetchLongArray(tif, dir, v)) {
			uint16 i;
			uint16 check_count;
			if ((uint64)samples<=count)
				check_count=samples;
			else
				check_count=(uint16)count;
			for (i=1; i<check_count; i++)
				if (v[i] != v[0]) {
					TIFFErrorExt(tif->tif_clientdata, tif->tif_name,
					    "Cannot handle different per-sample values for field \"%s\"",
					    _TIFFFieldWithTag(tif, dir->tdir_tag)->field_name);
					goto bad;
				}
			*pl = v[0];
			status = 1;
		}
	bad:
		if (v && v != buf)
			_TIFFfree(v);
	}
	return (status);
	#endif
}

/*
 * Fetch samples/pixel ANY values for the specified tag and verify that all
 * values are the same.
 */
static int
TIFFFetchPerSampleAnys(TIFF* tif, TIFFDirEntryUnion* dir, double* pl)
{
	#ifdef NDEF
	uint16 samples = tif->tif_dir.td_samplesperpixel;
	int status = 0;
	if (CheckDirCount(tif, dir, (uint32) samples)) {
		double buf[10];
		double* v = buf;
		uint64 count;
		if (!(tif->tif_flags&TIFF_BIGTIFF))
			count = dir->classic.tdir_count;
		else
			count = dir->big.tdir_count;
		if (count > NITEMS(buf))
			v = (double*) _TIFFCheckMalloc(tif, count, sizeof(double),
			    "to fetch per-sample values");
		if (v && TIFFFetchAnyArray(tif, dir, v)) {
			uint16 i;
			uint16 check_count;
			if ((uint64)samples<=count)
				check_count=samples;
			else
				check_count=(uint16)count;
			for (i=1; i<check_count; i++)
				if (v[i] != v[0]) {
					TIFFErrorExt(tif->tif_clientdata, tif->tif_name,
					    "Cannot handle different per-sample values for field \"%s\"",
					    _TIFFFieldWithTag(tif, dir->tdir_tag)->field_name);
					goto bad;
				}
			*pl = v[0];
			status = 1;
		}
	bad:
		if (v && v != buf)
			_TIFFfree(v);
	}
	return (status);
	#endif
}
#undef NITEMS

/*
 * Fetch a set of offsets or lengths.
 * While this routine says "strips", in fact it's also used for tiles.
 */
static int
TIFFFetchStripThing(TIFF* tif, TIFFDirEntryUnion* dir, long nstrips, uint64** lpp)
{
	enum TIFFReadDirEntryErr err;
	uint64 count;
	uint64* data;
	if (!(tif->tif_flags&TIFF_BIGTIFF))
		count=dir->classic.tdir_count;
	else
		count=dir->big.tdir_count;
	err=TIFFReadDirEntryLong8Array(tif,dir,&data);
	if (err!=TIFFReadDirEntryErrOk)
	{
		TIFFReadDirEntryOutputErr(tif,err,_TIFFFieldWithTag(tif,dir->common.tdir_tag)->field_name);
		return 0;
	}
	if (count!=(uint64)nstrips)
	{
		uint64* resizeddata;
		resizeddata=(uint64*)_TIFFCheckMalloc(tif,nstrips,sizeof(uint64),"for strip array");
		if (resizeddata==0)
			return 0;
		if (count<(uint64)nstrips)
		{
			_TIFFmemcpy(resizeddata,data,(uint32)count*sizeof(uint64));
			_TIFFmemset(resizeddata+(uint32)count,0,(nstrips-(uint32)count)*sizeof(uint64));
		}
		else
			_TIFFmemcpy(resizeddata,data,nstrips*sizeof(uint64));
		_TIFFfree(data);
		data=resizeddata;
	}
	*lpp=data;
}

/*
 * Fetch and set the RefBlackWhite tag.
 */
static int
TIFFFetchRefBlackWhite(TIFF* tif, TIFFDirEntryUnion* dir)
{
	assert(0);
	#ifdef NDEF

	static const char mesg[] = "for \"ReferenceBlackWhite\" array";
	char* cp;
	int ok;

	if (dir->tdir_type == TIFF_RATIONAL)
		return (TIFFFetchNormalTag(tif, dir));
	/*
	 * Handle LONG's for backward compatibility.
	 */
	cp = (char *)_TIFFCheckMalloc(tif, dir->tdir_count,
				      sizeof (uint32), mesg);
	if( (ok = (cp && TIFFFetchLongArray(tif, dir, (uint32*) cp))) != 0) {
		float* fp = (float*)
		    _TIFFCheckMalloc(tif, dir->tdir_count, sizeof (float), mesg);
		if( (ok = (fp != NULL)) != 0 ) {
			uint32 i;
			for (i = 0; i < dir->tdir_count; i++)
				fp[i] = (float)((uint32*) cp)[i];
			ok = TIFFSetField(tif, dir->tdir_tag, fp);
			_TIFFfree((char*) fp);
		}
	}
	if (cp)
		_TIFFfree(cp);
	return (ok);
	#endif
}

/*
 * Fetch and set the SubjectDistance EXIF tag.
 */
static int
TIFFFetchSubjectDistance(TIFF* tif, TIFFDirEntryUnion* dir)
{
	assert(0);
	#ifdef NDEF

	uint32 l[2];
	float v;
	int ok = 0;

	if (TIFFFetchData(tif, dir, (char *)l)
	    && cvtRational(tif, dir, l[0], l[1], &v)) {
		/*
		 * XXX: Numerator 0xFFFFFFFF means that we have infinite
		 * distance. Indicate that with a negative floating point
		 * SubjectDistance value.
		 */
		ok = TIFFSetField(tif, dir->tdir_tag,
				  (l[0] != 0xFFFFFFFF) ? v : -v);
	}

	return ok;
	#endif
}

/*
 * Replace a single strip (tile) of uncompressed data by multiple strips
 * (tiles), each approximately STRIP_SIZE_DEFAULT bytes. This is useful for
 * dealing with large images or for dealing with machines with a limited
 * amount memory.
 */
static void
ChopUpSingleUncompressedStrip(TIFF* tif)
{
	register TIFFDirectory *td = &tif->tif_dir;
	uint64 bytecount = td->td_stripbytecount[0];
	uint64 offset = td->td_stripoffset[0];
	uint64 rowbytes = TIFFVTileSize(tif, 1), stripbytes;
	tstrip_t strip, nstrips, rowsperstrip;
	uint64* newcounts;
	uint64* newoffsets;

	/*
	 * Make the rows hold at least one scanline, but fill specified amount
	 * of data if possible.
	 */
	if (rowbytes > STRIP_SIZE_DEFAULT) {
		stripbytes = rowbytes;
		rowsperstrip = 1;
	} else if (rowbytes > 0 ) {
		rowsperstrip = STRIP_SIZE_DEFAULT / rowbytes;
		stripbytes = rowbytes * rowsperstrip;
	}
	else
	    return;

	/*
	 * never increase the number of strips in an image
	 */
	if (rowsperstrip >= td->td_rowsperstrip)
		return;
	nstrips = (tstrip_t) TIFFhowmany(bytecount, stripbytes);
	if( nstrips == 0 ) /* something is wonky, do nothing. */
	    return;

	newcounts = (uint64*) _TIFFCheckMalloc(tif, nstrips, sizeof (uint64),
				"for chopped \"StripByteCounts\" array");
	newoffsets = (uint64*) _TIFFCheckMalloc(tif, nstrips, sizeof (uint64),
				"for chopped \"StripOffsets\" array");
	if (newcounts == NULL || newoffsets == NULL) {
		/*
		 * Unable to allocate new strip information, give up and use
		 * the original one strip information.
		 */
		if (newcounts != NULL)
			_TIFFfree(newcounts);
		if (newoffsets != NULL)
			_TIFFfree(newoffsets);
		return;
	}
	/*
	 * Fill the strip information arrays with new bytecounts and offsets
	 * that reflect the broken-up format.
	 */
	for (strip = 0; strip < nstrips; strip++) {
		if (stripbytes > bytecount)
			stripbytes = bytecount;
		newcounts[strip] = stripbytes;
		newoffsets[strip] = offset;
		offset += stripbytes;
		bytecount -= stripbytes;
	}
	/*
	 * Replace old single strip info with multi-strip info.
	 */
	td->td_stripsperimage = td->td_nstrips = nstrips;
	TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, rowsperstrip);

	_TIFFfree(td->td_stripbytecount);
	_TIFFfree(td->td_stripoffset);
	td->td_stripbytecount = newcounts;
	td->td_stripoffset = newoffsets;
	td->td_stripbytecountsorted = 1;
}

/* vim: set ts=8 sts=8 sw=8 noet: */
