//----------------------------------------------------------------------------
// License:  See top level LICENSE.txt file.
//
// Author:
//
// $Id: ossimKakaduImageMpiSWriterSequenceConnection.cpp 17206 2010-04-25 23:20:40Z dburken $
//----------------------------------------------------------------------------
//  RP - Modified for jpeg2000

#include <ossim/ossimConfig.h> /* To pick up OSSIM_HAS_MPI. */

#ifdef OSSIM_HAS_MPI
#  if OSSIM_HAS_MPI
#    include <mpi.h>
#  endif
#endif

#include <ossimKakaduImageMpiSWriterSequenceConnection.h>
#include <ossim/parallel/ossimMpi.h>
#include <ossim/imaging/ossimImageData.h>
#include <ossim/imaging/ossimImageDataFactory.h>
#include <ossim/base/ossimTrace.h>
#include <ossim/base/ossimEndian.h>
#include <ossim/base/ossimNotifyContext.h>
#include <ossim/base/ossimException.h>

static ossimTrace traceDebug = ossimTrace("ossimKakaduImageMpiSWriterSequenceConnection:debug");

RTTI_DEF1(ossimKakaduImageMpiSWriterSequenceConnection, "ossimKakaduImageMpiSWriterSequenceConnection", ossimImageSourceSequencer)

ossimKakaduImageMpiSWriterSequenceConnection::ossimKakaduImageMpiSWriterSequenceConnection(ossimObject* owner,
                                                                               long numberOfTilesToBuffer)
   :ossimImageSourceSequencer(NULL,
                              owner),
    theNumberOfTilesToBuffer(numberOfTilesToBuffer),
    m_compressor(new ossimKakaduCompressor())
{
   theRank = 0;
   theNumberOfProcessors = 1;
   theNumberOfTilesToBuffer = ((theNumberOfTilesToBuffer>0)?theNumberOfTilesToBuffer:2);

#ifdef OSSIM_HAS_MPI
#  if OSSIM_HAS_MPI
   MPI_Comm_rank(MPI_COMM_WORLD, &theRank);
   MPI_Comm_size(MPI_COMM_WORLD, &theNumberOfProcessors);
#  endif
#endif

   if(theRank!=0)
   {
      theCurrentTileNumber = theRank -1;
   }
   else
   {
      theCurrentTileNumber = 0;
   }
}

ossimKakaduImageMpiSWriterSequenceConnection::ossimKakaduImageMpiSWriterSequenceConnection(ossimImageSource* inputSource,
                                                                               ossimObject* owner,
                                                                               long numberOfTilesToBuffer)
   :ossimImageSourceSequencer(inputSource,
                                 owner),
    theNumberOfTilesToBuffer(numberOfTilesToBuffer)
{
   theRank = 0;
   theNumberOfProcessors = 1;
   theNumberOfTilesToBuffer = ((theNumberOfTilesToBuffer>0)?theNumberOfTilesToBuffer:2);
#if OSSIM_HAS_MPI
   MPI_Comm_rank(MPI_COMM_WORLD, &theRank);
   MPI_Comm_size(MPI_COMM_WORLD, &theNumberOfProcessors);
#endif
   if(theRank!=0)
   {
      theCurrentTileNumber = theRank -1;
   }
   else
   {
      theCurrentTileNumber = 0;
   }
}

ossimKakaduImageMpiSWriterSequenceConnection::~ossimKakaduImageMpiSWriterSequenceConnection()
{
   deleteOutputTiles();
   if (m_compressor)
   {
      delete m_compressor;
      m_compressor = 0;
   }
}

void ossimKakaduImageMpiSWriterSequenceConnection::deleteOutputTiles()
{
}

void ossimKakaduImageMpiSWriterSequenceConnection::initialize()
{
  ossimImageSourceSequencer::initialize();

  theCurrentTileNumber = theRank-1;
}

void ossimKakaduImageMpiSWriterSequenceConnection::setToStartOfSequence()
{
   ossimImageSourceSequencer::setToStartOfSequence();
   if(theRank != 0)
   {
      // we will subtract one since the masters job is just
      // writting and not issue getTiles.
      theCurrentTileNumber = theRank-1;

   }
   else
   {
      // the master will start at 0
      theCurrentTileNumber = 0;
   }
}

void ossimKakaduImageMpiSWriterSequenceConnection::slaveProcessTiles()
{
#ifdef OSSIM_HAS_MPI
#  if OSSIM_HAS_MPI
   ossimEndian endian;
   ossim_uint32 numberOfTiles    = getNumberOfTiles();
   int         errorValue; // Needed for MPI_Isend and MPI_Wait.
   MPI_Request request;    // Needed for MPI_Isend and MPI_Wait.

   request = MPI_REQUEST_NULL;

   if(traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG) << "DEBUG ossimKakaduImageMpiSWriterSequenceConnection::slaveProcessTiles(): entering slave and will look at " << numberOfTiles << " tiles" << std::endl;
   }
   while(theCurrentTileNumber < numberOfTiles)
   {
       request = MPI_REQUEST_NULL;
       std::ostringstream bos;

      ossimRefPtr<ossimImageData> data = ossimImageSourceSequencer::getTile(theCurrentTileNumber);

      kdu_core::kdu_dims fragment;
      ossim_uint32 width = data->getWidth();
      ossim_uint32 height = data->getHeight();
      ossimIpt tileRect = ossimIpt(width, height);
      fragment.size.x=width;
      fragment.size.y=height;
      fragment.pos.x=(theCurrentTileNumber%getNumberOfTilesHorizontal()) * width;
      fragment.pos.y=(theCurrentTileNumber/getNumberOfTilesHorizontal()) * height;

      m_compressor->setAlphaChannelFlag(false);

      m_compressor->setQualityType(ossimKakaduCompressor::OKP_NUMERICALLY_LOSSLESS);

      try
      {
         // Make a compressor

         m_compressor->create(&bos,
            getOutputScalarType(),
            getNumberOfOutputBands(),
            getAreaOfInterest(),
            tileRect,
            getNumberOfTiles(),
            false,
            &fragment,
            theCurrentTileNumber
         );

      }
      catch (const ossimException& e)
      {
       std::cout << "EXCEPTION: " << ossimString(e.what()) << "\n";
      }

      if (data.valid() && ( data->getDataObjectStatus() != OSSIM_NULL ) )
      {
        if ( ! m_compressor->writeTile( *(data.get()) ) )
        {
        }
      }
      else
      {
      }

      m_compressor->finish();


      errorValue = MPI_Wait(&request, MPI_STATUS_IGNORE);

      ossim_uint32 boslen = bos.str().length();

      const std::string bosstr = bos.str();
      char * buf = const_cast<char*>(bosstr.c_str());

      errorValue = MPI_Isend(buf,
                             boslen,
                             MPI_UNSIGNED_CHAR,
                             0,
                             0,
                             MPI_COMM_WORLD,
                             &request);

      errorValue = MPI_Wait(&request, MPI_STATUS_IGNORE);

      theCurrentTileNumber += (theNumberOfProcessors-1);
   }
#  endif
#endif
}


ossimRefPtr<ossimImageData> ossimKakaduImageMpiSWriterSequenceConnection::getNextTile(
   ossim_uint32 /* resLevel */)
{
   ossimNotify(ossimNotifyLevel_FATAL)
      << "FATAL ossimKakaduImageMpiSWriterSequenceConnection::getNextTile(): "
      << "should not be called" << std::endl;
   return ossimRefPtr<ossimImageData>();
}

void ossimKakaduImageMpiSWriterSequenceConnection::setProgressionOrder(const ossimString& corder) const
{
   if (m_compressor)
   {
     m_compressor->setProgressionOrderString(corder);
   }
}
