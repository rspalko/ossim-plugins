//----------------------------------------------------------------------------
//
// License:  LGPL
//
// See LICENSE.txt file in the top level directory for more details.
//
// Description: Sequencer for building overview files.
//
//----------------------------------------------------------------------------
// $Id: ossimKakaduOverviewSequencer.cpp 19724 2011-06-06 21:07:15Z dburken $

#include "ossimKakaduOverviewSequencer.h"
#include <ossim/base/ossimIpt.h>
#include <ossim/base/ossimIrect.h>
#include <ossim/base/ossimMultiResLevelHistogram.h>
#include <ossim/base/ossimNotify.h>
#include <ossim/base/ossimTrace.h>
#include <ossim/imaging/ossimImageData.h>
#include <ossim/imaging/ossimImageDataFactory.h>
#include <ossim/imaging/ossimImageHandler.h>
#include <ossim/imaging/ossimImageMetaData.h>
#include <ossim/parallel/ossimMpi.h>
#include <ossim/base/ossimException.h>
#include <ostream>


#ifdef OSSIM_ID_ENABLED
static const char OSSIM_ID[] = "$Id: ossimKakaduOverviewSequencer.cpp 19724 2011-06-06 21:07:15Z dburken $";
#endif

static ossimTrace traceDebug("ossimKakaduOverviewSequencer:debug");
static const ossimIpt DEFAULT_TILE_SIZE(1024, 1024);

ossimKakaduOverviewSequencer::ossimKakaduOverviewSequencer()
   :
   ossimReferenced(),
   m_imageHandler(0),
   m_maskWriter(0),
   m_maskFilter(0),
   m_tile(0),
   m_areaOfInterest(),
   m_tileSize(OSSIM_DEFAULT_TILE_WIDTH, OSSIM_DEFAULT_TILE_HEIGHT),
   m_numberOfTilesHorizontal(0),
   m_numberOfTilesVertical(0),
   m_currentTileNumber(0),
   m_sourceResLevel(0),
   m_dirtyFlag(true),
   m_decimationFactor(2),
   m_resampleType(ossimFilterResampler::ossimFilterResampler_BOX),
   m_histogram(0),
   m_histoMode(OSSIM_HISTO_MODE_UNKNOWN),
   m_histoTileIndex(1),
   m_scanForMinMax(false),
   m_scanForMinMaxNull(false),
   m_minValues(0),
   m_maxValues(0),
   m_nulValues(0),
   m_compressor(new ossimKakaduCompressor())
{
   m_areaOfInterest.makeNan();

   if (traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << "ossimKakaduOverviewSequencer::ossimKakaduOverviewSequencer entered..."
         << "\nmpi rank:  "<< ossimMpi::instance()->getRank()
         << std::endl;
#ifdef OSSIM_ID_ENABLED
      ossimNotify(ossimNotifyLevel_DEBUG)<< "OSSIM_ID:  " << OSSIM_ID
                                         << std::endl;
#endif
   }
}

ossimKakaduOverviewSequencer::~ossimKakaduOverviewSequencer()
{
   m_imageHandler = 0;
   m_maskFilter   = 0;
   m_maskWriter   = 0;
   m_tile         = 0;
   m_histogram    = 0;

   if (traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << "ossimKakaduOverviewSequencer::~ossimKakaduOverviewSequencer entered..."
         << "\nmpi rank:  " << ossimMpi::instance()->getRank()
         << std::endl;
   }
   if (m_compressor)
   {
      delete m_compressor;
      m_compressor = 0;
   }
}

ossim_uint32 ossimKakaduOverviewSequencer::getNumberOfTiles() const
{
   return (m_numberOfTilesHorizontal*m_numberOfTilesVertical);
}

ossim_uint32 ossimKakaduOverviewSequencer::getNumberOfTilesHorizontal()const
{
   return m_numberOfTilesHorizontal;
}

ossim_uint32 ossimKakaduOverviewSequencer::getNumberOfTilesVertical()const
{
   return m_numberOfTilesVertical;
}

void ossimKakaduOverviewSequencer::getOutputImageRectangle(ossimIrect& rect) const
{
   if (m_areaOfInterest.hasNans())
   {
      rect.makeNan(); // not initialized...
   }
   else
   {
      ossim_int32 width  = m_areaOfInterest.width()  / m_decimationFactor;
      ossim_int32 height = m_areaOfInterest.height() / m_decimationFactor;

      //---
      // NOTE:
      // This will add a sample or line if there is a remainder like:
      // 1025 / 2 = 512 will then become 513.  It will be up to the resample
      // tile method to fill the extra sample correctly.
      //---
      if (m_areaOfInterest.width() % m_decimationFactor)
      {
         ++width;
      }
      if (m_areaOfInterest.height() % m_decimationFactor)
      {
         ++height;
      }

      rect = ossimIrect(0, 0, width-1, height-1);
   }
}

void ossimKakaduOverviewSequencer::setImageHandler(ossimImageHandler* input)
{
   m_imageHandler = input;
   m_areaOfInterest.makeNan();
   m_dirtyFlag = true;

   if (m_maskWriter.valid())
      m_maskWriter->connectMyInputTo(m_imageHandler.get());
}

void ossimKakaduOverviewSequencer::setSourceLevel(ossim_uint32 level)
{
   m_sourceResLevel = level;
   m_areaOfInterest.makeNan();
   m_dirtyFlag = true;
}

ossimHistogramMode ossimKakaduOverviewSequencer::getHistogramMode() const
{
   return m_histoMode;
}

void ossimKakaduOverviewSequencer::setHistogramMode(ossimHistogramMode mode)
{
   m_histoMode = mode;
   m_dirtyFlag = true;
}

void ossimKakaduOverviewSequencer::writeHistogram()
{
   if ( m_histogram.valid() && m_imageHandler.valid() )
   {
      writeHistogram( m_imageHandler->getFilenameWithThisExtension("his") );
   }
}

void ossimKakaduOverviewSequencer::writeHistogram(const ossimFilename& file)
{
   if ( m_histogram.valid() )
   {
      ossimRefPtr<ossimMultiResLevelHistogram> histo = new ossimMultiResLevelHistogram;
      histo->addHistogram( m_histogram.get() );
      ossimKeywordlist kwl;
      histo->saveState(kwl);
      kwl.write(file.c_str());
   }
}

void ossimKakaduOverviewSequencer::initialize()
{
   if ( m_dirtyFlag == false )
   {
      return; // Already initialized.
   }

   if ( !m_imageHandler )
   {
      return;
   }

   ossimImageSource* imageSource = m_imageHandler.get();
   if (m_maskFilter.valid())
      imageSource = m_maskFilter.get();

   // Check the area of interest and set from image if needed.
   if ( m_areaOfInterest.hasNans() )
   {
      m_areaOfInterest = m_imageHandler->getImageRectangle(m_sourceResLevel);
   }

   // Check the tile size and set from image if needed.
   if ( m_tileSize.hasNans() )
   {
      m_tileSize.x = imageSource->getTileWidth();
      m_tileSize.y = imageSource->getTileHeight();
   }

   // Update m_numberOfTilesHorizontal and m_numberOfTilesVertical.
   updateTileDimensions();

   // Start on first tile.
   m_currentTileNumber = 0;

   // Use this factory constructor as it copies the min/max/nulls from the image handler.
   m_tile = ossimImageDataFactory::instance()->
      create( 0, imageSource->getNumberOfOutputBands(), imageSource);

   if(m_tile.valid())
   {
      // Set the width and height.
      m_tile->setWidthHeight(static_cast<ossim_uint32>(m_tileSize.x),
                             static_cast<ossim_uint32>(m_tileSize.y) );
      // Initialize tile buffer.
      m_tile->initialize();
   }

   if (m_histoMode != OSSIM_HISTO_MODE_UNKNOWN)
   {
      m_histogram = new ossimMultiBandHistogram;

      m_histogram->create(imageSource);

      if (m_histoMode == OSSIM_HISTO_MODE_NORMAL)
      {
         m_histoTileIndex = 1; // Sample every tile.
      }
      else
      {
         const ossim_float64 PIXEL_TO_SAMPLE = 100.0 * 256.0 * 256.0; // 100 256x256 tiles.
         ossim_float64 pixels = m_tileSize.x * m_tileSize.y * getNumberOfTiles();

         if (traceDebug())
         {
            ossimNotify(ossimNotifyLevel_DEBUG)
               << "pixels: " << pixels << "\n"
               << "PIXEL_TO_SAMPLE: " << PIXEL_TO_SAMPLE << "\n";
         }

         if (pixels > PIXEL_TO_SAMPLE)
         {
            m_histoTileIndex = ossim::round<ossim_uint32>(pixels/PIXEL_TO_SAMPLE);
         }
         else
         {
            m_histoTileIndex = 1; // Sample every tile.
         }
      }
   }
   else
   {
      m_histogram = 0;
   }


   if ( m_scanForMinMax || m_scanForMinMaxNull )
   {
      //---
      // The methods ossimImageData::computeMinMaxNullPix and computeMinMaxPix
      // will resize and set min to max, max to min and null to max if the arrays passed in
      // are empty.
      //---
      clearMinMaxNullArrays();
   }

   m_dirtyFlag = false;

   if (traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << "aoi:                      " << m_areaOfInterest
         << "\ntile size:              " << m_tileSize
         << "\ntiles wide:             " << m_numberOfTilesHorizontal
         << "\ntiles high:             " << m_numberOfTilesVertical
         << "\nsource rlevel:          " << m_sourceResLevel
         << "\ndecimation factor:      " << m_decimationFactor
         << "\nresamp type:            " << m_resampleType
         << "\nscan for min max:       " << (m_scanForMinMax?"true\n":"false\n")
         << "\nscan for min, max null: " << (m_scanForMinMaxNull?"true\n":"false\n")
         << "\nhisto mode:             " << m_histoMode << "\n";
      if (m_histoMode != OSSIM_HISTO_MODE_UNKNOWN)
      {
         ossimNotify(ossimNotifyLevel_DEBUG)
            << "Sampling every " << m_histoTileIndex << " tile(s) for histogram.\n";
      }
   }
}

void ossimKakaduOverviewSequencer::setToStartOfSequence()
{
   m_currentTileNumber = 0;
}

bool ossimKakaduOverviewSequencer::getNextTile(std::ostream& bos)
{
   if ( m_dirtyFlag )
   {
     return false;
   }

   // Get the rectangle to grab from the image.
   ossimIrect inputRect;
   getInputTileRectangle(inputRect);

   // Get the output rectangle.
   ossimIrect outputRect;
   getOutputTileRectangle(outputRect);

   // Capture the output rectangle.
   m_tile->setImageRectangle(outputRect);

   // Start with a blank tile.
   m_tile->makeBlank();

   // Grab the input tile.
   ossimRefPtr<ossimImageData> inputTile;
   if (m_maskFilter.valid())
      inputTile = m_maskFilter->getTile(inputRect, m_sourceResLevel);
   else
      inputTile = m_imageHandler->getTile(inputRect, m_sourceResLevel);

   if (inputTile.valid() == false)
   {
      ossimNotify(ossimNotifyLevel_WARN)
         << "ossimKakaduOverviewSequencer::getNextTile DEBUG:"
         << "\nRequest failed for input rect: " << inputRect
         << "\nRes level:  " << m_sourceResLevel << std::endl;
   }

   if ( m_scanForMinMaxNull )
   {
      inputTile->computeMinMaxNulPix(m_minValues, m_maxValues, m_nulValues);
   }
   else if ( m_scanForMinMax )
   {
      inputTile->computeMinMaxPix(m_minValues, m_maxValues);
   }

   if ( ( m_histoMode != OSSIM_HISTO_MODE_UNKNOWN ) &&
        ( (m_currentTileNumber % m_histoTileIndex) == 0 ) )
   {
      if (traceDebug())
      {
         ossimNotify(ossimNotifyLevel_DEBUG)
            << "ossimKakaduOverviewSequencer::getNextTile DEBUG:"
            << "\npopulating histogram for tile: " << m_currentTileNumber
            << "\n";
      }
      inputTile->populateHistogram(m_histogram);
   }

   if ( (inputTile->getDataObjectStatus() == OSSIM_PARTIAL) ||
        (inputTile->getDataObjectStatus() == OSSIM_FULL ) )
   {
      // Resample the tile.
      resampleTile(inputTile.get());
      m_tile->validate();

      // Scan the resampled pixels for bogus values to be masked out (if masking enabled)
      if (m_maskWriter.valid())
         m_maskWriter->generateMask(m_tile, m_sourceResLevel+1);
   }

   kdu_core::kdu_dims fragment;
   fragment.size.x=m_tileSize.x;
   fragment.size.y=m_tileSize.y;
   fragment.pos.x=(m_currentTileNumber%getNumberOfTilesHorizontal()) * m_tileSize.x;
   fragment.pos.y=(m_currentTileNumber/getNumberOfTilesHorizontal()) * m_tileSize.y;

   if (m_compressor->getAlphaChannelFlag())
   {
      m_compressor->setAlphaChannelFlag(false);
   }

   if ( ossim::getActualBitsPerPixel(m_imageHandler->getOutputScalarType()) > 31 )
   {
      m_compressor->setQualityType(ossimKakaduCompressor::OKP_VISUALLY_LOSSLESS);
   }

   try
   {
      // Make a compressor
      ossimIrect imageRect;
      getOutputImageRectangle(imageRect);
      m_compressor->create(&bos,
         m_imageHandler->getOutputScalarType(),
         m_imageHandler->getNumberOfOutputBands(),
         imageRect,
         m_tileSize,
         getNumberOfTiles(),
        false,
         &fragment,
         m_currentTileNumber
      );
   }
   catch (const ossimException& e)
   {
   }

   if (m_tile.valid() && ( m_tile->getDataObjectStatus() != OSSIM_NULL ) )
   {
     if ( ! m_compressor->writeTile( *(m_tile.get()) ) )
     {
     }
   }
   else
   {
   }
   m_compressor->finish();

   // Increment the tile index.
   ++m_currentTileNumber;
   return true;
}

void ossimKakaduOverviewSequencer::slaveProcessTiles()
{
}

bool ossimKakaduOverviewSequencer::isMaster() const
{
   return true;
}

ossimIpt ossimKakaduOverviewSequencer::getTileSize() const
{
   return m_tileSize;
}

void ossimKakaduOverviewSequencer::setTileSize(const ossimIpt& tileSize)
{
   m_tileSize = tileSize;
   updateTileDimensions();
   m_dirtyFlag = true;
}

void ossimKakaduOverviewSequencer::setResampleType(
   ossimFilterResampler::ossimFilterResamplerType resampleType)
{
   m_resampleType = resampleType;
}

void ossimKakaduOverviewSequencer::setScanForMinMax(bool flag)
{
   m_scanForMinMax  = flag;
}

bool ossimKakaduOverviewSequencer::getScanForMinMax() const
{
   return m_scanForMinMax;
}

void ossimKakaduOverviewSequencer::setScanForMinMaxNull(bool flag)
{
   m_scanForMinMaxNull = flag;
}

bool ossimKakaduOverviewSequencer::getScanForMinMaxNull() const
{
   return m_scanForMinMaxNull;
}

void ossimKakaduOverviewSequencer::clearMinMaxNullArrays()
{
   m_minValues.clear();
   m_maxValues.clear();
   m_nulValues.clear();
}

bool ossimKakaduOverviewSequencer::writeOmdFile(const std::string& file)
{
   static const char M[] = "ossimKakaduOverviewSequencer::writeOmdFile";
   if ( traceDebug() )
   {
      ossimNotify(ossimNotifyLevel_DEBUG) << M << " entered...\nfile: " << file << endl;
   }

   //---
   // This method writes an omd file to disk.  Typically called after sequencing trough tile that
   // were scanned for min, max, and potentially null values.
   // Since this can be called without a scan there is extra logic in here to initialize values
   // if so.  Also there are sanity checks for cases where there is no null value, i.e. a
   // full tile, in which case assumptions have to be made...
   //---

   bool result = false;
   if ( file.size() && m_imageHandler.valid() )
   {
      const ossim_uint32 BANDS = m_imageHandler->getNumberOfInputBands();
      const ossimScalarType SCALAR = m_imageHandler->getOutputScalarType();
      const ossim_float64 DEFAULT_NUL = ossim::defaultNull(SCALAR);
      const ossim_float64 FALLBACK_NULL = -32767; // This is my arbitrary pick. (drb)

      if ( traceDebug() )
      {
         ossimNotify(ossimNotifyLevel_DEBUG)
            << "Original array values:\n";
         std::vector<ossim_float64>::const_iterator i = m_minValues.begin();
         ossim_int32 band = 0;
         while ( i < m_minValues.end() )
         {
            ossimNotify(ossimNotifyLevel_DEBUG)
               << "min[" << band++ << "]: " << *i << "\n";
            ++i;
         }
         i = m_maxValues.begin();
         band = 0;
         while ( i < m_maxValues.end() )
         {
            ossimNotify(ossimNotifyLevel_DEBUG)
               << "max[" << band++ << "]: " << *i << "\n";
            ++i;
         }
         i = m_nulValues.begin();
         band = 0;
         while ( i < m_nulValues.end() )
         {
            ossimNotify(ossimNotifyLevel_DEBUG)
               << "nul[" << band++ << "]: " << *i << "\n";
            ++i;
         }
      }

      if ( (m_scanForMinMax == true) || (m_nulValues.size() !=  BANDS) )
      {
         // Only scanned for min and max so set the null.
         if (m_nulValues.size() !=  BANDS)
         {
            m_nulValues.resize(BANDS);
         }
         for (ossim_uint32 band = 0; band < BANDS; ++band)
         {
            m_nulValues[band] = m_imageHandler->getNullPixelValue(band);
         }
      }
      else if ( m_scanForMinMaxNull == true )
      {
         //---
         // The arrays (sizes) should be set if we did the scan.
         // Note that scanning for null only works if there IS a null in the image; hence, the
         // extra sanity checks as if there are no null then the null gets set to the real min.
         //
         // This is very dangerous code as it makes assumptions (drb)...
         //---
         if ( (m_minValues.size() ==  BANDS) &&
              (m_maxValues.size() ==  BANDS) &&
              (m_nulValues.size() ==  BANDS) )
         {
            switch(SCALAR)
            {
               case OSSIM_UINT8:
               case OSSIM_SINT8:
               case OSSIM_UINT16:
               case OSSIM_USHORT11:
               case OSSIM_USHORT12:
               case OSSIM_USHORT13:
               case OSSIM_USHORT14:
               case OSSIM_USHORT15:
               case OSSIM_UINT32:
               {
                  // All of these should have a null of 0.
                  for (ossim_uint32 band = 0; band < BANDS; ++band)
                  {
                     if ( m_nulValues[band] > DEFAULT_NUL )
                     {
                        if ( m_nulValues[band] < m_minValues[band] )
                        {
                           m_minValues[band] = m_nulValues[band];
                        }
                        m_nulValues[band] = DEFAULT_NUL;
                     }
                  }
                  break;
               }
               case OSSIM_SINT16:
               {
                  for (ossim_uint32 band = 0; band < BANDS; ++band)
                  {
                     if ( ( m_nulValues[band] != DEFAULT_NUL ) && // -32768
                          ( m_nulValues[band] != -32767.0 )    &&
                          ( m_nulValues[band] != -32766.0 )    &&
                          ( m_nulValues[band] != -9999.0 ) )
                     {
                        if ( ( m_nulValues[band] > -9999.0 ) &&
                             ( m_nulValues[band] < m_minValues[band] ) )
                        {
                           m_minValues[band] = m_nulValues[band];
                        }
                        m_nulValues[band] = FALLBACK_NULL;
                     }
                  }
                  break;
               }
               case OSSIM_SINT32:
               {
                  for (ossim_uint32 band = 0; band < BANDS; ++band)
                  {
                     if ( ( m_nulValues[band] != DEFAULT_NUL ) &&
                          ( m_nulValues[band] != -32768.0 ) &&        // Common null
                          ( m_nulValues[band] != -32767.0 ) &&       // The other common null.
                          ( m_nulValues[band] != -32766.0 ) &&
                          ( m_nulValues[band] != -9999.0  ) &&
                          ( m_nulValues[band] != -99999.0 ) )
                     {
                        if ( ( m_nulValues[band] > -9999.0 ) &&
                             ( m_nulValues[band] < m_minValues[band] ) )
                        {
                           m_minValues[band] = m_nulValues[band];
                        }
                        m_nulValues[band] = FALLBACK_NULL;
                     }
                  }
                  break;
               }
               case OSSIM_FLOAT32:
               case OSSIM_FLOAT64:
               {
                  for (ossim_uint32 band = 0; band < BANDS; ++band)
                  {
                     if ( ( m_nulValues[band] != DEFAULT_NUL ) &&
                          ( m_nulValues[band] != -32768.0 ) &&
                          ( m_nulValues[band] != -32767.0 ) &&
                          ( m_nulValues[band] != -32766.0 ) &&
                          ( m_nulValues[band] != -9999.0  ) &&
                          ( m_nulValues[band] != -99999.0 ) )
                     {
                        if ( ( m_nulValues[band] > -9999 ) &&
                             ( m_nulValues[band] < m_minValues[band] ) )
                        {
                           m_minValues[band] = m_nulValues[band];
                        }
                        m_nulValues[band] = FALLBACK_NULL;
                     }
                  }
                  break;
               }
               case OSSIM_NORMALIZED_FLOAT:
               case OSSIM_NORMALIZED_DOUBLE:
               {
                  for (ossim_uint32 band = 0; band < BANDS; ++band)
                  {
                     if ( ( m_nulValues[band] != DEFAULT_NUL ) &&
                          ( m_nulValues[band] > 0.0) )
                     {
                        if ( m_nulValues[band] < m_minValues[band] )
                        {
                           m_minValues[band] = m_nulValues[band];
                        }
                        m_nulValues[band] = 0.0;
                     }
                  }
                  break;
               }
               case OSSIM_SCALAR_UNKNOWN:
               default:
               {
                  if(traceDebug())
                  {
                     ossimNotify(ossimNotifyLevel_DEBUG)
                        << __FILE__ << ":" << __LINE__
                        << " " << M << "\nUnhandled scalar type:  " << SCALAR << std::endl;
                  }
                  break;
               }

            } // End: switch(SCALAR)

         } // Matches: if ( (m_minValues.size() ==  BANDS) &&...
         else
         {
            // ERROR!
            ossimNotify(ossimNotifyLevel_WARN)
               << M << "ERROR:\nMin, max and null array sizes bad! No omd file will be written."
               << std::endl;
         }

      } // Matches: else if ( m_scanForMinMaxNull == true )
      else
      {
         // Get the values from the image handler.
         if (m_minValues.size() !=  BANDS)
         {
            m_minValues.resize(BANDS);
            for (ossim_uint32 band = 0; band < BANDS; ++band)
            {
               m_minValues[band] = m_imageHandler->getMinPixelValue(band);
            }
         }
         if (m_maxValues.size() !=  BANDS)
         {
            m_maxValues.resize(BANDS);
            for (ossim_uint32 band = 0; band < BANDS; ++band)
            {
               m_maxValues[band] = m_imageHandler->getMaxPixelValue(band);
            }
         }
         if (m_nulValues.size() !=  BANDS)
         {
            m_nulValues.resize(BANDS);
            for (ossim_uint32 band = 0; band < BANDS; ++band)
            {
               m_nulValues[band] = m_imageHandler->getNullPixelValue(band);
            }
         }
      }

      // Last size check as the m_scanForMinMaxNull block could have failed.
      if ( ( m_minValues.size() ==  BANDS ) &&
           ( m_maxValues.size() ==  BANDS ) &&
           ( m_nulValues.size() ==  BANDS ) )
      {
         // Write the omd file:
         ossimKeywordlist kwl;
         if( ossimFilename(file).exists())
         {
            // Pick up existing omd file.
            kwl.addFile(file.c_str());
         }
         ossimImageMetaData metaData(SCALAR, BANDS);
         for(ossim_uint32 band = 0; band < BANDS; ++band)
         {
            metaData.setMinPix(band,  m_minValues[band]);
            metaData.setMaxPix(band,  m_maxValues[band]);
            metaData.setNullPix(band, m_nulValues[band]);
         }
         // Save to keyword list.
         metaData.saveState(kwl);

         // Write to disk.
         result = kwl.write(file.c_str());
         if ( result )
         {
            ossimNotify(ossimNotifyLevel_NOTICE) << "Wrote file: " << file << "\n";
         }
         else
         {
            ossimNotify(ossimNotifyLevel_WARN) << "ERROR writing file: " << file << "\n";
         }
      }

      if ( traceDebug() )
      {
         ossimNotify(ossimNotifyLevel_DEBUG)
            << "Final array values:\n";
         std::vector<ossim_float64>::const_iterator i = m_minValues.begin();
         ossim_int32 band = 0;
         while ( i < m_minValues.end() )
         {
            ossimNotify(ossimNotifyLevel_DEBUG)
               << "min[" << band++ << "]: " << *i << "\n";
            ++i;
         }
         i = m_maxValues.begin();
         band = 0;
         while ( i < m_maxValues.end() )
         {
            ossimNotify(ossimNotifyLevel_DEBUG)
               << "max[" << band++ << "]: " << *i << "\n";
            ++i;
         }
         i = m_nulValues.begin();
         band = 0;
         while ( i < m_nulValues.end() )
         {
            ossimNotify(ossimNotifyLevel_DEBUG)
               << "nul[" << band++ << "]: " << *i << "\n";
            ++i;
         }
      }

   } // Matches: if ( file && m_imageHandler.valid() )

   if ( traceDebug() )
   {
      ossimNotify(ossimNotifyLevel_DEBUG) << M << " exit status" << (result?"true\n":"false\n");
   }

   return result;
}

void ossimKakaduOverviewSequencer::getInputTileRectangle(ossimIrect& inputRect) const
{
   if (!m_imageHandler) return;

   getOutputTileRectangle(inputRect);
   inputRect = inputRect * m_decimationFactor;
   inputRect = inputRect;

#if 0
   if (traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << "ossimKakaduOverviewSequencer::getInputTileRectangle DEBUG:"
         << "input rect: " << inputRect << std::endl;
   }
#endif
}

void ossimKakaduOverviewSequencer::getOutputTileRectangle(
   ossimIrect& outputRect) const
{
   // Get the row and column.
   ossim_int32 row = m_currentTileNumber / m_numberOfTilesHorizontal;
   ossim_int32 col = m_currentTileNumber % m_numberOfTilesHorizontal;

   ossimIpt pt;

   // Set the upper left.
   pt.y = row * m_tileSize.y;
   pt.x = col * m_tileSize.x;
   outputRect.set_ul(pt);

   // Set the lower right.
   pt.y = pt.y + m_tileSize.y - 1;
   pt.x = pt.x + m_tileSize.x - 1;
   outputRect.set_lr(pt);

#if 0
   if (traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << "ossimKakaduOverviewSequencer::getOutputTileRectangle DEBUG:"
         << "output rect: " << outputRect << std::endl;
   }
#endif
}

void ossimKakaduOverviewSequencer::updateTileDimensions()
{
   if( m_areaOfInterest.hasNans() || m_tileSize.hasNans() )
   {
      m_numberOfTilesHorizontal = 0;
      m_numberOfTilesVertical   = 0;
      m_dirtyFlag = true;
   }
   else
   {
      // Get the output rectangle.
      ossimIrect rect;
      getOutputImageRectangle(rect);

      m_numberOfTilesHorizontal =
         static_cast<ossim_uint32>( rect.width()  / m_tileSize.x );
      m_numberOfTilesVertical =
         static_cast<ossim_uint32>( rect.height() / m_tileSize.y );

      if (rect.width()  % m_tileSize.x)
      {
         ++m_numberOfTilesHorizontal;
      }
      if (rect.height() % m_tileSize.y)
      {
         ++m_numberOfTilesVertical;
      }
   }
}

void ossimKakaduOverviewSequencer::resampleTile(const ossimImageData* inputTile)
{
   switch(m_imageHandler->getOutputScalarType())
   {
      case OSSIM_UINT8:
      {
         resampleTile(inputTile, ossim_uint8(0));
         break;
      }
      case OSSIM_USHORT11:
      case OSSIM_USHORT12:
      case OSSIM_USHORT13:
      case OSSIM_USHORT14:
      case OSSIM_USHORT15:
      case OSSIM_UINT16:
      {
         resampleTile(inputTile, ossim_uint16(0));
         break;
      }
      case OSSIM_SINT16:
      {
         resampleTile(inputTile, ossim_sint16(0));
         break;
      }

      case OSSIM_UINT32:
      {
         resampleTile(inputTile, ossim_uint32(0));
         break;
      }

      case OSSIM_SINT32:
      {
         resampleTile(inputTile, ossim_sint32(0));
         break;
      }

      case OSSIM_FLOAT32:
      {
         resampleTile(inputTile, ossim_float32(0.0));
         break;
      }

      case OSSIM_NORMALIZED_DOUBLE:
      case OSSIM_FLOAT64:
      {
         resampleTile(inputTile, ossim_float64(0.0));
         break;
      }
      default:
         ossimNotify(ossimNotifyLevel_WARN)
            << "ossimKakaduOverviewSequencer::resampleTile Unkown pixel type!"
            << std::endl;
         return;

   } // End of "switch(m_imageHandler->getOutputScalarType())"
}

template <class T>
void  ossimKakaduOverviewSequencer::resampleTile(const ossimImageData* inputTile, T  /* dummy */ )
{
#if 0
   if (traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << "ossimKakaduOverviewSequencer::resampleTile DEBUG: "
         << "\ncurrent tile: " << m_currentTileNumber
         << "\ninput tile:\n" << *inputTile
         << "output tile:\n" << *(m_tile.get())
         << endl;
   }
#endif

   const ossim_uint32 BANDS = m_tile->getNumberOfBands();
   const ossim_uint32 LINES = m_tile->getHeight();
   const ossim_uint32 SAMPS = m_tile->getWidth();
   const ossim_uint32 INPUT_WIDTH = m_decimationFactor*m_tileSize.x;

   T nullPixel              = 0;
   ossim_float64 weight     = 0.0;
   ossim_float64 value      = 0.0;
   ossim_uint32 sampOffset  = 0;

   if (m_resampleType == ossimFilterResampler::ossimFilterResampler_NEAREST_NEIGHBOR)
   {
      for (ossim_uint32 band=0; band<BANDS; ++band)
      {
         const T* s = static_cast<const T*>(inputTile->getBuf(band)); // source
         T*       d = static_cast<T*>(m_tile->getBuf(band)); // destination

         nullPixel = static_cast<T>(inputTile->getNullPix(band));
         weight = 0.0;
         value  = 0.0;

         for (ossim_uint32 i=0; i<LINES; ++i)
         {
            for (ossim_uint32 j=0; j<SAMPS; ++j)
            {
               sampOffset = j*m_decimationFactor;

               weight = 1.0;
               value  = *(s + i*m_decimationFactor*INPUT_WIDTH + sampOffset);

               if(weight)
               {
                  d[j] = static_cast<T>( value/weight );
               }
               else
               {
                  d[j] = nullPixel;
               }

            } // End of sample loop.

            d += m_tileSize.x;

         } // End of line loop.

      } // End of band loop.

   }
   else // ossimFilterResampler::ossimFilterResampler_BOX
   {
      ossim_uint32 lineOffset1 = 0;
      ossim_uint32 lineOffset2 = 0;
      ossim_float64 ul = 0.0;
      ossim_float64 ur = 0.0;
      ossim_float64 ll = 0.0;
      ossim_float64 lr = 0.0;

      for (ossim_uint32 band=0; band<BANDS; ++band)
      {
         const T* s = static_cast<const T*>(inputTile->getBuf(band)); // source
         T*       d = static_cast<T*>(m_tile->getBuf(band)); // destination

         nullPixel = static_cast<T>(inputTile->getNullPix(band));
         weight = 0.0;
         value  = 0.0;

         for (ossim_uint32 i=0; i<LINES; ++i)
         {
            lineOffset1 = i*m_decimationFactor*INPUT_WIDTH;
            lineOffset2 = (i*m_decimationFactor+1)*INPUT_WIDTH;

            for (ossim_uint32 j=0; j<SAMPS; ++j)
            {
               sampOffset = j*m_decimationFactor;

               weight = 0.0;
               value  = 0.0;

               //---
               // Grab four pixels from the source, average, and assign
               // to output.
               //---
               ul = *(s + lineOffset1 + sampOffset);
               ur = *(s + lineOffset1 + sampOffset + 1);
               ll = *(s + lineOffset2 + sampOffset);
               lr = *(s + lineOffset2 + sampOffset + 1);

               if(ul != nullPixel)
               {
                  ++weight;
                  value += ul;
               }
               if(ur != nullPixel)
               {
                  ++weight;
                  value += ur;
               }
               if(ll != nullPixel)
               {
                  ++weight;
                  value += ll;
               }
               if(lr != nullPixel)
               {
                  ++weight;
                  value += lr;
               }

               if(weight)
               {
                  d[j] = static_cast<T>( value/weight );
               }
               else
               {
                  d[j] = nullPixel;
               }

            } // End of sample loop.

            d += m_tileSize.x;

         } // End of line loop.

      } // End of band loop.
   }
}

void ossimKakaduOverviewSequencer::setBitMaskObjects(ossimBitMaskWriter* mask_writer,
                                               ossimMaskFilter* mask_filter)
{
   m_maskWriter = mask_writer;
   m_maskFilter = mask_filter;
}

bool ossimKakaduOverviewSequencer::setProperty(ossimRefPtr<ossimProperty> property)
{
   if ( property.valid() )
   {
      m_compressor->setProperty(property);
   }
}
