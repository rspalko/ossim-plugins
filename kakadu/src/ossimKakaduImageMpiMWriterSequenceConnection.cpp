//*******************************************************************
//
// License:  See top level LICENSE.txt file.
//
// Author:
//
// Description:
//
// This class is specific to the Master connection and is optimized
// for the writer sequence and batch processing chain.  For the Slave
// implementation look for the ossimImageMpiSWriterSequenceConnection.cc and .h
// files.  The Master connection is currently implemented to allways do
// a recieve and does no processing itself.  The slave connection does
// all the actual work and processing.
//
//*******************************************************************
//  $Id: ossimKakaduImageMpiMWriterSequenceConnection.cpp 12099 2007-12-01 16:09:36Z dburken $
//  RP - modified for JPEG2000


#include <ossim/ossimConfig.h> /* To pick up OSSIM_HAS_MPI. */

#ifdef OSSIM_HAS_MPI
#  if OSSIM_HAS_MPI
#    include <mpi.h>
#  endif
#endif

#include <ossimKakaduImageMpiMWriterSequenceConnection.h>
#include <ossim/parallel/ossimMpi.h>
#include <ossim/imaging/ossimImageData.h>
#include <ossim/imaging/ossimImageDataFactory.h>
#include <ossim/base/ossimTrace.h>
#include <ossim/base/ossimEndian.h>

static ossimTrace traceDebug = ossimTrace("ossimKakaduImageMpiMWriterSequenceConnection:debug");

RTTI_DEF1(ossimKakaduImageMpiMWriterSequenceConnection, "ossimKakaduImageMpiMWriterSequenceConnection", ossimImageSourceSequencer)


ossimKakaduImageMpiMWriterSequenceConnection::ossimKakaduImageMpiMWriterSequenceConnection(
   ossimImageSource* inputSource,
   ossimObject* owner)
   :ossimImageSourceSequencer(inputSource, owner),
   m_tlmGenerator(),
   m_tlmBytes(0)
{
   theRank = 0;
   theNumberOfProcessors = 1;

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
   theNeedToSendRequest = true;
}

ossimKakaduImageMpiMWriterSequenceConnection::ossimKakaduImageMpiMWriterSequenceConnection(ossimObject* owner)
   :ossimImageSourceSequencer(NULL, owner)
{
   theRank = 0;
   theNumberOfProcessors = 1;

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
   theNeedToSendRequest = true;
}

ossimKakaduImageMpiMWriterSequenceConnection::~ossimKakaduImageMpiMWriterSequenceConnection()
{
   m_tlmGenerator.clear();
}

void ossimKakaduImageMpiMWriterSequenceConnection::initialize()
{
  ossimImageSourceSequencer::initialize();

  theCurrentTileNumber = theRank;//-1;
}

void ossimKakaduImageMpiMWriterSequenceConnection::setToStartOfSequence()
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

bool ossimKakaduImageMpiMWriterSequenceConnection::getNextTileStream(std::ostream& bos)
{
#if OSSIM_HAS_MPI

   ossim_uint32 numberOfTiles = getNumberOfTiles();
   // Had to move this here in order to wait on setAreaOfInterest() to be called for the case where we are generating a chip
   // Number of tiles reflects the AOI rect in that case
   if(theCurrentTileNumber == 0)
   {
     m_maxTileParts = 1;
     m_tlmGenerator.init(numberOfTiles, m_maxTileParts, 2, 4);
     m_tlmBytes = m_tlmGenerator.get_tlm_bytes();
   }
   int errorValue = 0;

   if(theCurrentTileNumber >= numberOfTiles)
   {
      return false;
   }

   int count;

   MPI_Status status;
   MPI_Probe(theCurrentTileNumber%(theNumberOfProcessors-1)+1, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

   MPI_Get_count( &status,  MPI_UNSIGNED_CHAR, &count );

   const char * buf = new char[count];

   errorValue = MPI_Recv((void*)buf,
                         count,
                         MPI_UNSIGNED_CHAR,
                         theCurrentTileNumber%(theNumberOfProcessors-1)+1,
                         0,
                         MPI_COMM_WORLD,
                         &status);

   bos.write(buf, count);

   // ADJUST for JPEG2000 header based on manual scan of first tile to TLM (Need to do this because we need the compressed data size without the header and TLM.)
   if (theCurrentTileNumber == 0)
   {
     int idx = 0;
     int end = count - 1;

     while (idx < end-1)
     {
        if ((unsigned char) buf[idx] == 0xff && (unsigned char) buf[idx+1] == 0x55) // TLM marker = ff55
        {
          count-=(idx+m_tlmBytes);
          break;
        }
        idx++;
     }
   }

   delete [] buf;

   // RP blank tile-part is 14 bytes
   count-=(14*(m_maxTileParts-1));

   m_tlmGenerator.add_tpart_length(theCurrentTileNumber, count);
   int idx = 1;
   // ADD the blank tile-parts if they were specified
   while (idx < m_maxTileParts)
   {
        m_tlmGenerator.add_tpart_length(theCurrentTileNumber, 14);
        idx++;
   }
   ++theCurrentTileNumber;
   if (theCurrentTileNumber == getNumberOfTiles())
   {
      ossimKakaduCompressedTarget* target = new ossimKakaduCompressedTarget();
         target->setStream(&bos);

     // This is a brand new TLM since we scrapped the dummy in the slave code.  No previous tile-parts or bytes written.
     m_tlmGenerator.write_tlms(target,
                               0,
                               0);
     bos.seekp(0, std::ios_base::end);
     if (target)
     {
        delete target;
        target = 0;
     }
   }

   return true;
#endif
}

ossimRefPtr<ossimImageData> ossimKakaduImageMpiMWriterSequenceConnection::getNextTile(
   ossim_uint32 /* resLevel */)
{
   ossimNotify(ossimNotifyLevel_FATAL)
      << "FATAL ossimKakaduImageMpiMWriterSequenceConnection::getNextTile(): "
      << "should not be called" << std::endl;
   return ossimRefPtr<ossimImageData>();
}
