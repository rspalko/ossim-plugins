///----------------------------------------------------------------------------
//
// License:  See top level LICENSE.txt file.
//
// Author:  RP - derived from David Burken ossimMpiMasterOverviewSequencer.cpp
//
// Description: Class definition for mpi master sequencer for building
// overview files.
//
//----------------------------------------------------------------------------
// $Id: ossimKakaduMpiMasterOverviewSequencer.cpp 17870 2010-08-12 13:12:32Z sbortman $

#include "ossimKakaduMpiMasterOverviewSequencer.h"
#include <ossim/ossimConfig.h> /* To pick up OSSIM_HAS_MPI. */
#include <ossim/base/ossimEndian.h>
#include <ossim/parallel/ossimMpi.h>


#if OSSIM_HAS_MPI
#  include <mpi.h>
#endif

ossimKakaduMpiMasterOverviewSequencer::ossimKakaduMpiMasterOverviewSequencer()
   :
   ossimKakaduOverviewSequencer(),
   m_rank(0),
   m_numberOfProcessors(1),
   m_tlmGenerator(),
   m_tlmBytes(0),
   m_maxTileParts(1)
{
#if OSSIM_HAS_MPI
   MPI_Comm_rank(MPI_COMM_WORLD, &m_rank);
   MPI_Comm_size(MPI_COMM_WORLD, &m_numberOfProcessors);
#endif

   //---
   // Since this is the master sequencer it should always be rank 0; else,
   // we have a coding error.  Since we have the getNextTile implemented to
   // receive from the slave processes we always start the tile count at 0.
   //---
   m_currentTileNumber = 0;
}

ossimKakaduMpiMasterOverviewSequencer::~ossimKakaduMpiMasterOverviewSequencer()
{
   m_tlmGenerator.clear();
}

void ossimKakaduMpiMasterOverviewSequencer::initialize()
{
   ossimKakaduOverviewSequencer::initialize();
   // RP - one tile-part is sufficient
   //m_maxTileParts = (getNumberOfTiles() < 255) ? getNumberOfTiles() : 255;
   m_tlmGenerator.init(getNumberOfTiles(), m_maxTileParts, 2, 4);
   m_tlmBytes = m_tlmGenerator.get_tlm_bytes();
}

void ossimKakaduMpiMasterOverviewSequencer::setToStartOfSequence()
{
   m_currentTileNumber = 0;
}

bool ossimKakaduMpiMasterOverviewSequencer::getNextTile(std::ostream& bos)
{
   if ( m_dirtyFlag )
   {
      //---
      // If this happens we have a coding error.  Someone started sequencing
      // without initializing us properly.
      //---
      return false;
   }
   //---
   // Using mpi.  The slaves will send us tiles to be returned by this method.
   // They will alway be sent in big endian (network byte order) so we must
   // swap back if the scalar type is not 8 bit and the system byte order is
   // little endian. We will use the endian pointer itself as a flag to swap.
   //---
   int         errorValue;

   // Total number of tiles to process...
   ossim_uint32 numberOfTiles = getNumberOfTiles();

   if(m_currentTileNumber >= numberOfTiles)
   {
      return false;
   }

   int count;

#if OSSIM_HAS_MPI
   if(ossimMpi::instance()->getNumberOfProcessors() > 1)
   {
     MPI_Status status;
     MPI_Probe(m_currentTileNumber%(m_numberOfProcessors-1)+1, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

     MPI_Get_count( &status,  MPI_UNSIGNED_CHAR, &count );

     const char * buf = new char[count];

     errorValue = MPI_Recv((void*)buf,
                         count,
                         MPI_UNSIGNED_CHAR,
                         m_currentTileNumber%(m_numberOfProcessors-1)+1,
                         0,
                         MPI_COMM_WORLD,
                         &status);

     bos.write(buf, count);

     // ADJUST for JPEG2000 header based on manual scan of first tile to TLM (Need to do this because we need the compressed data size without the header and TLM.)
     if (m_currentTileNumber == 0)
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
   }
   else // Just do everything in the master if we are not MPI or a single processor
   {
#endif
     std::ostringstream tmpos;

     ossimKakaduOverviewSequencer::getNextTile(tmpos);
     // We re-increment later
     m_currentTileNumber--;
     count = tmpos.str().length();
     bos.write(tmpos.str().c_str(), count);

     const std::string tmpstr = tmpos.str();
     char * buf = const_cast<char*>(tmpstr.c_str());
     if (m_currentTileNumber == 0)
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
#if OSSIM_HAS_MPI
   }
#endif

   // RP blank tile-part is 14 bytes
   count-=(14*(m_maxTileParts-1));

   m_tlmGenerator.add_tpart_length(m_currentTileNumber, count);
   int idx = 1;
   // ADD the blank tile-parts if they were specified
   while (idx < m_maxTileParts)
   {
       m_tlmGenerator.add_tpart_length(m_currentTileNumber, 14);
       idx++;
   }

   // Increment the tile index.
   ++m_currentTileNumber;
   if (m_currentTileNumber == getNumberOfTiles())
   {
      ossimKakaduCompressedTarget* target = new ossimKakaduCompressedTarget();
         target->setStream(&bos);

     // This is a brand new TLM since we scrapped the dummy in the slave code.  No previous tile-parts or bytes written.
     m_tlmGenerator.write_tlms(target,
                               0,
                               0);
     if (target)
     {
        delete target;
        target = 0;
     }
   }

   return true;

}
