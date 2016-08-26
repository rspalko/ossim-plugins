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
// $Id: ossimKakaduMpiSlaveOverviewSequencer.h 17194 2010-04-23 15:05:19Z dburken $
#ifndef ossimKakaduMpiSlaveOverviewSequencer_HEADER
#define ossimKakaduMpiSlaveOverviewSequencer_HEADER

#include "ossimKakaduOverviewSequencer.h"
#include <ossim/base/ossimConstants.h>

/**
 * @class MPI slave sequencer for building overview files.
 */
class OSSIM_DLL ossimKakaduMpiSlaveOverviewSequencer : public ossimKakaduOverviewSequencer
{
public:

   /** default constructor */
   ossimKakaduMpiSlaveOverviewSequencer();


   /**
    * @return Always false for this implementation.
    */
   virtual bool isMaster()const;

   /**
    * This must be called.  We can only initialize this
    * object completely if we know all connections
    * are valid.  Some other object drives this and so the
    * connection's initialize will be called after.  The job
    * of this connection is to set up the sequence.  It will
    * default to the bounding rect.  The area of interest can be
    * set to some other rectagle (use setAreaOfInterest).
    */
   virtual void initialize();

   /**
    * @brief Will set the internal pointers to the upperleft
    * tile number.  To go to the next tile in the sequence
    * just call getNextTile.
    */
   virtual void setToStartOfSequence();

   /**
    * @brief This is a trigger to start resampling tiles.
    */
   virtual void slaveProcessTiles();

protected:
   /** virtual destructor */
   virtual ~ossimKakaduMpiSlaveOverviewSequencer();

   int  m_rank;
   int  m_numberOfProcessors;
};

#endif /* #ifndef ossimKakaduMpiSlaveOverviewSequencer_HEADER */
