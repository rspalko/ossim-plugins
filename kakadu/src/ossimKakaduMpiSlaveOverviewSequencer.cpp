//----------------------------------------------------------------------------
//
// License:  See top level LICENSE.txt file.
//
// Author:  
//
// Description: Class definition for mpi slave sequencer for building
// overview files.
//
//----------------------------------------------------------------------------
// $Id: ossimKakaduMpiSlaveOverviewSequencer.cpp 17870 2010-08-12 13:12:32Z sbortman $

#include "ossimKakaduMpiSlaveOverviewSequencer.h"
#include <ossim/ossimConfig.h>        /* To pick up OSSIM_HAS_MPI. */
#include <ossim/base/ossimCommon.h>   /* For get byte order. */
#include <ossim/base/ossimEndian.h>
#include <ossim/base/ossimRefPtr.h>
#include <ossim/imaging/ossimImageData.h>



#if OSSIM_HAS_MPI
#  include <mpi.h>
#endif

ossimKakaduMpiSlaveOverviewSequencer::ossimKakaduMpiSlaveOverviewSequencer()
   :
   ossimKakaduOverviewSequencer(),
   m_rank(0),
   m_numberOfProcessors(1)
{
#if OSSIM_HAS_MPI
   MPI_Comm_rank(MPI_COMM_WORLD, &m_rank);
   MPI_Comm_size(MPI_COMM_WORLD, &m_numberOfProcessors);
#endif

   if( m_rank != 0 )
   {
      //---
      // Master process (rank 0) does not resample tiles so our process rank
      // is 1 then we'll start at tile 0, rank 2 starts at 1, rank 3 starts
      // at 2 and so on...
      //---
      m_currentTileNumber = m_rank -1;
   }
   else
   {
      m_currentTileNumber = 0;
   }
}

ossimKakaduMpiSlaveOverviewSequencer::~ossimKakaduMpiSlaveOverviewSequencer()
{
}

bool ossimKakaduMpiSlaveOverviewSequencer::isMaster()const
{
   return false;
}

void ossimKakaduMpiSlaveOverviewSequencer::initialize()
{
   ossimKakaduOverviewSequencer::initialize();

   m_currentTileNumber = m_rank-1;
}

void ossimKakaduMpiSlaveOverviewSequencer::setToStartOfSequence()
{
   if(m_rank != 0)
   {
      //---
      // Subtract one since the masters job is just writing and not issue
      // getTiles.
      //---
      m_currentTileNumber = m_rank-1;
   }
   else
   {
      // the master will start at 0
      m_currentTileNumber = 0;
   }
}

void ossimKakaduMpiSlaveOverviewSequencer::slaveProcessTiles()
{
#if OSSIM_HAS_MPI

   // Total number of tiles for all processes.
   ossim_uint32 numberOfTiles = getNumberOfTiles();

   int         errorValue; // Needed for MPI_Isend and MPI_Wait.
   MPI_Request request;    // Needed for MPI_Isend and MPI_Wait.

   while(m_currentTileNumber < numberOfTiles)
   {
      std::ostringstream bos;
      ossimKakaduOverviewSequencer::getNextTile(bos);
      ossim_uint32 boslen = bos.str().length();

      const std::string bosstr = bos.str();
      char * buf = const_cast<char*>(bosstr.c_str());

      // Send the buffer to the master process.
      request = MPI_REQUEST_NULL;
      errorValue = MPI_Isend(buf,
                            boslen,
                             MPI_UNSIGNED_CHAR,
                             0,
                             0,
                             MPI_COMM_WORLD,
                             &request);

      //---
      // Wait for send to complete before we overwrite the buffer with the
      // next tile.
      //---
      errorValue = MPI_Wait(&request, MPI_STATUS_IGNORE);

      //---
      // If we have eight processes only seven are used for resampling tiles,
      // so we would process every seventh tile.
      //
      // The call to getNextTile has already incremented m_currentTileNumber
      // by one so adjust accordingly.
      //---
      if (m_numberOfProcessors>2)
      {
         m_currentTileNumber += (m_numberOfProcessors-2);
      }

   } // End of while loop through number of tiles.

#endif /* End of #if OSSIM_HAS_MPI */

}
