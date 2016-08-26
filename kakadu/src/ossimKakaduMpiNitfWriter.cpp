//----------------------------------------------------------------------------
//
// License:  LGPL
//
// See LICENSE.txt file in the top level directory for more details.
//
// Description: OSSIM Kakadu based nitf writer.
//
//----------------------------------------------------------------------------
// $Id: ossimKakaduMpiNitfWriter.cpp 20422 2012-01-05 20:40:26Z dburken $

#include "ossimKakaduMpiNitfWriter.h"
#include "ossimKakaduCommon.h"
#include "ossimKakaduCompressor.h"
#include "ossimKakaduKeywords.h"
#include "ossimKakaduMessaging.h"

#include <ossim/base/ossimBooleanProperty.h>
#include <ossim/base/ossimDate.h>
#include <ossim/base/ossimDpt.h>
#include <ossim/base/ossimEndian.h>
#include <ossim/base/ossimException.h>
#include <ossim/base/ossimKeywordlist.h>
#include <ossim/base/ossimKeywordNames.h>
#include <ossim/base/ossimNumericProperty.h>
#include <ossim/base/ossimTrace.h>
#include <ossim/base/ossimStringProperty.h>

#include <ossim/projection/ossimMapProjection.h>

#include <ossim/imaging/ossimImageData.h>
#include <ossim/imaging/ossimImageSource.h>

#include <ossim/support_data/ossimNitfCommon.h>
#include <ossim/support_data/ossimNitfFileHeader.h>
#include <ossim/support_data/ossimNitfFileHeaderV2_1.h>
#include <ossim/support_data/ossimNitfImageHeader.h>
#include <ossim/support_data/ossimNitfImageHeaderV2_1.h>
#include <ossim/support_data/ossimNitfTagInformation.h>

#include <ostream>

static const ossimIpt DEFAULT_TILE_SIZE(1024, 1024);

RTTI_DEF1(ossimKakaduMpiNitfWriter, "ossimKakaduMpiNitfWriter", ossimNitfWriterBase)

//---
// For trace debugging (to enable at runtime do:
// your_app -T "ossimKakaduMpiNitfWriter:debug" your_app_args
//---
static ossimTrace traceDebug("ossimKakaduMpiNitfWriter:debug");

//---
// For the "ident" program which will find all exanded $Id: ossimKakaduMpiNitfWriter.cpp 20422 2012-01-05 20:40:26Z dburken $
// them.
//---
#if OSSIM_ID_ENABLED
static const char OSSIM_ID[] = "$Id: ossimKakaduMpiNitfWriter.cpp 20422 2012-01-05 20:40:26Z dburken $";
#endif

ossimKakaduMpiNitfWriter::ossimKakaduMpiNitfWriter()
   : ossimNitfWriterBase(),
     m_outputStream(0),
     m_ownsStreamFlag(false),
     m_imageHeader(0),
     m_fileHeader(0),
     m_useSourceFileHeader(false),
     m_useSourceImageHeader(false)
{
   if (traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << "ossimKakaduMpiNitfWriter::ossimKakaduMpiNitfWriter entered"
         << std::endl;
#if OSSIM_ID_ENABLED
      ossimNotify(ossimNotifyLevel_DEBUG)
         << "OSSIM_ID:  "
         << OSSIM_ID
         << std::endl;
#endif
   }
   m_imageHeader      = new ossimNitfImageHeaderV2_1;
   m_fileHeader       = new ossimNitfFileHeaderV2_1;

   //---
   // Since the internal nitf tags are not very accurate, write an external
   // geometry out as default behavior.  Users can disable this via the
   // property interface or keyword list.
   //---
   // No longer applicable - changing to false
   setWriteExternalGeometryFlag(false);

   // Set the output image type in the base class.
   setOutputImageType(getShortName());
}

ossimKakaduMpiNitfWriter::~ossimKakaduMpiNitfWriter()
{
   // This will flush stream and delete it if we own it.
   close();

}

ossimString ossimKakaduMpiNitfWriter::getShortName() const
{
   return ossimString("ossim_kakadu_mpi_nitf_j2k");
}

ossimString ossimKakaduMpiNitfWriter::getLongName() const
{
   return ossimString("ossim kakadu mpi nitf j2k writer");
}

ossimString ossimKakaduMpiNitfWriter::getClassName() const
{
   return ossimString("ossimKakaduMpiNitfWriter");
}

bool ossimKakaduMpiNitfWriter::writeFile()
{
   // This method is called from ossimImageFileWriter::execute().

   bool result = false;

   if( theInputConnection.valid() &&
       (getErrorStatus() == ossimErrorCodes::OSSIM_OK) )
   {
      // Set the tile size for all processes.
      theInputConnection->setTileSize( DEFAULT_TILE_SIZE );
      theInputConnection->setToStartOfSequence();

      //---
      // Note only the master process used for writing...
      //---
      if(theInputConnection->isMaster())
      {
         if (!isOpen())
         {
            open();
         }

         if ( isOpen() )
         {
            result = writeStream();
         }
      }
      else // Slave process.
      {
         // This will return after all tiles for this node have been processed.
         theInputConnection->slaveProcessTiles();

         result = true;
      }
   }

   return result;
}

bool ossimKakaduMpiNitfWriter::writeStream()
{
   static const char MODULE[] = "ossimKakaduMpiNitfWriter::writeStream";

   if (traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << MODULE << " entered..." << endl;
   }

   if ( !theInputConnection || !m_outputStream || !theInputConnection->isMaster() )
   {
      return false;
   }

   const ossim_uint32    TILES  = theInputConnection->getNumberOfTiles();
   const ossim_uint32    BANDS  = theInputConnection->getNumberOfOutputBands();
   const ossimScalarType SCALAR = theInputConnection->getOutputScalarType();

   std::streampos endOfFileHdrPos;
   std::streampos endOfImgHdrPos;
   std::streampos endOfFilePos;
   std::streampos endOfImgPos;

   // Container record withing NITF file header.
   ossimNitfImageInfoRecordV2_1 imageInfoRecord;

   // NITF file header.

   // Note the sub header length and image length will be set later.
   // RP - only supporting one entry chip for right now and if we copy an existing header then it already has an image info record, but may have more that need removed
   if (!m_useSourceFileHeader)
   {
     m_fileHeader->addImageInfoRecord(imageInfoRecord);

     m_fileHeader->setDate(ossimDate());
     m_fileHeader->setTitle(ossimString("")); // ???
   }
   else
   {
     while (m_fileHeader->getNumberOfImages() > 1)
     {
       m_fileHeader->deleteLastImageInfoRecord();
     }
     while (m_fileHeader->getNumberOfDataExtSegments())
     {
        m_fileHeader->deleteLastDataExtSegInfoRecord();
     }
   }

   // Set the compression type:
   // This changes if we go from uncompressed to compressed
   m_imageHeader->setCompression(ossimString("C8"));

   // These values won't change if we are converting an existing NITF
   if (!m_useSourceImageHeader)
   {
   // Set the Image Magnification (IMAG) field.
   m_imageHeader->setImageMagnification(ossimString("1.0"));

   // Set the pixel type (PVTYPE) field.
   m_imageHeader->setPixelType(ossimNitfCommon::getNitfPixelType(SCALAR));

   // Set the actual bits per pixel (ABPP) field.
   m_imageHeader->setActualBitsPerPixel(ossim::getActualBitsPerPixel(SCALAR));

   // Set the bits per pixel (NBPP) field.
   m_imageHeader->setBitsPerPixel(ossim::getBitsPerPixel(SCALAR));

   m_imageHeader->setNumberOfBands(BANDS);
   m_imageHeader->setImageMode('B'); // IMODE field to blocked.

   if( (BANDS == 3) && (SCALAR == OSSIM_UCHAR) )
   {
      m_imageHeader->setRepresentation("RGB");
      m_imageHeader->setCategory("MS");
   }
   else if(BANDS == 1)
   {
      m_imageHeader->setRepresentation("MONO");
      m_imageHeader->setCategory("VIS");
   }
   else
   {
      m_imageHeader->setRepresentation("MULTI");
      m_imageHeader->setCategory("MS");
   }

   ossimNitfImageBandV2_1 bandInfo;
   for(ossim_uint32 band = 0; band < BANDS; ++band)
   {

      bandInfo.setBandRepresentation("M");
      m_imageHeader->setBandInfo(band, bandInfo);
   }
   }

   ossim_uint32 outputTilesWide = theInputConnection->getNumberOfTilesHorizontal();
   ossim_uint32 outputTilesHigh = theInputConnection->getNumberOfTilesVertical();

   m_imageHeader->setBlocksPerRow(outputTilesWide);
   m_imageHeader->setBlocksPerCol(outputTilesHigh);
   m_imageHeader->setNumberOfPixelsPerBlockRow(DEFAULT_TILE_SIZE.y);
   m_imageHeader->setNumberOfPixelsPerBlockCol(DEFAULT_TILE_SIZE.x);
   m_imageHeader->setNumberOfRows(theInputConnection->getAreaOfInterest().height());
   m_imageHeader->setNumberOfCols(theInputConnection->getAreaOfInterest().width());

   // Write the geometry info to the image header.
   writeGeometry(m_imageHeader.get(), theInputConnection.get());

   // Get the overflow tags from the file header and the image subheader
   takeOverflowTags(true, true);
   takeOverflowTags(true, false);
   takeOverflowTags(false, true);
   takeOverflowTags(false, false);

   for (vector<ossimNitfDataExtensionSegmentV2_1>::iterator iter = m_dataExtensionSegments.begin();
      iter != m_dataExtensionSegments.end(); iter++)
   {
      ossimNitfDataExtSegInfoRecordV2_1 desInfoRecord;
      iter->setSecurityMarkings(*m_fileHeader);
      std::ostringstream headerOut;
      headerOut << std::setw(4)
                << std::setfill('0')
                << std::setiosflags(ios::right)
                << iter->getHeaderLength();
      strcpy(desInfoRecord.theDataExtSegSubheaderLength, headerOut.str().c_str());

      std::ostringstream dataOut;
      dataOut << std::setw(9)
                << std::setfill('0')
                << std::setiosflags(ios::right)
                << iter->getDataLength();
      strcpy(desInfoRecord.theDataExtSegLength, dataOut.str().c_str());

      m_fileHeader->addDataExtSegInfoRecord(desInfoRecord);
   }

   // Write to stream capturing the stream position for later.
   m_fileHeader->writeStream(*m_outputStream);
   endOfFileHdrPos = m_outputStream->tellp();

   // Write the image header to stream capturing the stream position.
   m_imageHeader->writeStream(*m_outputStream);
   endOfImgHdrPos = m_outputStream->tellp();

   if (traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << MODULE << " DEBUG:"
         << "\noutputTilesWide:  " << outputTilesWide
         << "\noutputTilesHigh:  " << outputTilesHigh
         << "\nnumberOfTiles:    " << TILES
         << "\nimageRect: " << theInputConnection->getAreaOfInterest()
         << std::endl;
   }

   // Tile loop in the line direction.
   ossim_uint32 tileNumber = 0;
   bool result = true;
   for(ossim_uint32 y = 0; y < outputTilesHigh; ++y)
   {
      // Tile loop in the sample (width) direction.
      for(ossim_uint32 x = 0; x < outputTilesWide; ++x)
      {
         // Grab the resampled tile.
         //ossimRefPtr<ossimImageData> t =

         theInputConnection->getNextTileStream(*m_outputStream);
         //m_outputStream->write(t->getUcharBuf(), t->getSizeInBytes());

         // Increment tile number for percent complete.
         ++tileNumber;

      } // End of tile loop in the sample (width) direction.

      if (needsAborting())
      {
         setPercentComplete(100.0);
         break;
      }
      else
      {
         ossim_float64 tile = tileNumber;
         ossim_float64 numTiles = TILES;
         setPercentComplete(tile / numTiles * 100.0);
      }

   } // End of tile loop in the line (height) direction.

   endOfImgPos = m_outputStream->tellp();

   for (vector<ossimNitfDataExtensionSegmentV2_1>::iterator iter = m_dataExtensionSegments.begin();
      iter != m_dataExtensionSegments.end(); iter++)
   {
      iter->writeStream(*m_outputStream);
   }

   // Get the file length.
   endOfFilePos = m_outputStream->tellp();

   //---
   // Seek back to set some things that were not know until now and
   // rewrite the nitf file and image header.
   //---
   m_outputStream->seekp(0, std::ios_base::beg);

   // Set the file length.
   std::streamoff length = endOfFilePos;
   m_fileHeader->setFileLength(static_cast<ossim_uint64>(length));

   // Set the file header length.
   length = endOfFileHdrPos;
   m_fileHeader->setHeaderLength(static_cast<ossim_uint64>(length));
   // Set the image sub header length.
   length = endOfImgHdrPos - endOfFileHdrPos;

   imageInfoRecord.setSubheaderLength(static_cast<ossim_uint64>(length));

   // Set the image length.
   length = endOfImgPos - endOfImgHdrPos;
   imageInfoRecord.setImageLength(static_cast<ossim_uint64>(length));

   m_fileHeader->replaceImageInfoRecord(0, imageInfoRecord);

   if(!m_useSourceFileHeader) setComplexityLevel(m_fileHeader.get(), theInputConnection->getAreaOfInterest().width(), theInputConnection->getAreaOfInterest().height());

   // Rewrite the header.
   m_fileHeader->writeStream(*m_outputStream);

   // Set the compression rate now that the image size is known.
   ossimString comrat = ossimNitfCommon::getCompressionRate(
      theInputConnection->getAreaOfInterest(),
      BANDS,
      SCALAR,
      static_cast<ossim_uint64>(length));

   if(!m_useSourceImageHeader) m_imageHeader->setCompressionRateCode(comrat);

   // Rewrite the image header.
   m_imageHeader->writeStream(*m_outputStream);

   close();

   if (traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << MODULE << " exit status = " << (result?"true":"false\n")
         << std::endl;
   }

   return result;
}

bool ossimKakaduMpiNitfWriter::saveState(ossimKeywordlist& kwl,
                                      const char* prefix)const
{
   return ossimNitfWriterBase::saveState(kwl, prefix);
}

bool ossimKakaduMpiNitfWriter::loadState(const ossimKeywordlist& kwl,
                                      const char* prefix)
{
   return ossimNitfWriterBase::loadState(kwl, prefix);
}

bool ossimKakaduMpiNitfWriter::isOpen() const
{
   return (m_outputStream) ? true : false;
}

bool ossimKakaduMpiNitfWriter::open()
{
   bool result = false;

   close();

   // Check for empty filenames.
   if (theFilename.size())
   {
      std::ofstream* os = new std::ofstream();
      os->open(theFilename.c_str(), ios::out | ios::binary);
      if(os->is_open())
      {
         m_outputStream = os;
         m_ownsStreamFlag = true;
         result = true;
      }
      else
      {
         delete os;
         os = 0;
      }
   }

   if (traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << "ossimKakaduMpiNitfWriter::open()\n"
         << "File " << theFilename << (result ? " opened" : " not opened")
         << std::endl;
    }

   return result;
}

void ossimKakaduMpiNitfWriter::close()
{
   if (m_outputStream)
   {
      m_outputStream->flush();

      if (m_ownsStreamFlag)
      {
         delete m_outputStream;
         m_outputStream = 0;
         m_ownsStreamFlag = false;
      }
   }
}

ossimString ossimKakaduMpiNitfWriter::getExtension() const
{
   return ossimString("ntf");
}

bool ossimKakaduMpiNitfWriter::getOutputHasInternalOverviews( void ) const
{
   return true;
}

void ossimKakaduMpiNitfWriter::getImageTypeList(std::vector<ossimString>& imageTypeList)const
{
   imageTypeList.push_back(getShortName());
}

bool ossimKakaduMpiNitfWriter::hasImageType(const ossimString& imageType) const
{
   bool result = false;
   if ( (imageType == getShortName()) ||
        (imageType == "image/ntf") )
   {
      result = true;
   }
   return result;
}

void ossimKakaduMpiNitfWriter::setProperty(ossimRefPtr<ossimProperty> property)
{
}

ossimRefPtr<ossimProperty> ossimKakaduMpiNitfWriter::getProperty(
   const ossimString& name)const
{
   ossimRefPtr<ossimProperty> p = 0;

   if ( !p )
   {
      p = ossimNitfWriterBase::getProperty(name);
   }

   return p;
}

void ossimKakaduMpiNitfWriter::getPropertyNames(
   std::vector<ossimString>& propertyNames)const
{
   ossimNitfWriterBase::getPropertyNames(propertyNames);
}

bool ossimKakaduMpiNitfWriter::setOutputStream(std::ostream& stream)
{
   if (m_ownsStreamFlag && m_outputStream)
   {
      delete m_outputStream;
   }
   m_outputStream = &stream;
   m_ownsStreamFlag = false;
   return true;
}

void ossimKakaduMpiNitfWriter::addRegisteredTag(
   ossimRefPtr<ossimNitfRegisteredTag> registeredTag)
{
   ossimNitfTagInformation tagInfo;
   tagInfo.setTagData(registeredTag.get());
   m_imageHeader->addTag(tagInfo);
}

void ossimKakaduMpiNitfWriter::addRegisteredTag(ossimRefPtr<ossimNitfRegisteredTag> registeredTag,
   bool unique)
{
   addRegisteredTag(registeredTag, unique, 1, ossimString("IXSHD"));
}

void ossimKakaduMpiNitfWriter::addRegisteredTag(ossimRefPtr<ossimNitfRegisteredTag> registeredTag,
   bool unique, const ossim_uint32& ownerIndex, const ossimString& tagType)
{
   ossimNitfTagInformation tagInfo;
   tagInfo.setTagData(registeredTag.get());
   tagInfo.setTagType(tagType);

   switch (ownerIndex)
   {
      case 0:
      {
         m_fileHeader->addTag(tagInfo, unique);
         break;
      }

      case 1:
      {
         m_imageHeader->addTag(tagInfo, unique);
         break;
      }

      default:
      {
         // Do nothing
      }
   }
}

void ossimKakaduMpiNitfWriter::setFileHeaderV2_1(ossimRefPtr<ossimNitfFileHeaderV2_1> fHdr, bool preferSource)
{
       m_useSourceFileHeader = preferSource;
       m_fileHeader = fHdr;
}

void ossimKakaduMpiNitfWriter::setImageHeaderV2_1(ossimRefPtr<ossimNitfImageHeaderV2_1> iHdr, bool preferSource)
{
        m_useSourceImageHeader = preferSource;
        m_imageHeader = iHdr;
}

void ossimKakaduMpiNitfWriter::addDataExtensionSegment(const ossimNitfDataExtensionSegmentV2_1& des, bool allowTreOverflow)
{
   if (allowTreOverflow == false)
   {
      ossimRefPtr<ossimProperty> pId = des.getProperty(ossimNitfDataExtensionSegmentV2_1::DESID_KW);
      if (pId == NULL || pId->valueToString() == "TRE_OVERFLOW" ||
         pId->valueToString() == "REGISTERED EXTENSIONS" || pId->valueToString() == "CONTROLLED EXTENSIONS")
      {
         return;
      }
   }

   m_dataExtensionSegments.push_back(des);
}

void ossimKakaduMpiNitfWriter::takeOverflowTags(bool useFileHeader, bool userDefinedTags)
{
   ossimString itemIndex;
   std::vector<ossimNitfTagInformation> overflowTags;
   const ossim_uint32 potentialDesIndex = m_dataExtensionSegments.size() + 1;

   if (useFileHeader)
   {
      m_fileHeader->takeOverflowTags(overflowTags, potentialDesIndex, userDefinedTags);
      itemIndex = "0";
   }
   else
   {
      m_imageHeader->takeOverflowTags(overflowTags, potentialDesIndex, userDefinedTags);
      itemIndex = "1";
   }

   if (overflowTags.empty() == false)
   {
      ossimNitfDataExtensionSegmentV2_1 des;
      ossimRefPtr<ossimProperty> pDe =
         new ossimStringProperty(ossimNitfDataExtensionSegmentV2_1::DE_KW, "DE");
      des.setProperty(pDe);

      ossimRefPtr<ossimProperty> pId =
         new ossimStringProperty(ossimNitfDataExtensionSegmentV2_1::DESID_KW, "TRE_OVERFLOW");
      des.setProperty(pId);

      ossimRefPtr<ossimProperty> pVersion =
         new ossimStringProperty(ossimNitfDataExtensionSegmentV2_1::DESVER_KW, "1");
      des.setProperty(pVersion);

      ossimRefPtr<ossimProperty> pOverflow =
         new ossimStringProperty(ossimNitfDataExtensionSegmentV2_1::DESOFLW_KW, overflowTags[0].getTagType());
      des.setProperty(pOverflow);

      ossimRefPtr<ossimProperty> pItem =
         new ossimStringProperty(ossimNitfDataExtensionSegmentV2_1::DESITEM_KW, itemIndex);
      des.setProperty(pItem);

      des.setTagList(overflowTags);
      addDataExtensionSegment(des, true);
   }
}
