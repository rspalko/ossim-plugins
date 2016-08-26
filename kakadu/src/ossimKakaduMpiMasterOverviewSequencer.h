//----------------------------------------------------------------------------
//
// License:  See top level LICENSE.txt file.
//
// Author:  
//
// Description: Class definition for mpi master sequencer for building
// overview files.
//
//----------------------------------------------------------------------------
// $Id: ossimKakaduMpiMasterOverviewSequencer.h 17194 2010-04-23 15:05:19Z dburken $
#ifndef ossimKakaduMpiMasterOverviewSequencer_HEADER
#define ossimKakaduMpiMasterOverviewSequencer_HEADER

#include "ossimKakaduOverviewSequencer.h"
#include <ossim/base/ossimConstants.h>
#include <ossim/base/ossimRefPtr.h>
#include <ossim/imaging/ossimImageData.h>
#include <kdu_compressed.h>
//#include <compressed_local.h>
#include "ossimKakaduCompressedTarget.h"


/**
 * @class MPI master sequencer for building overview files.
 */
class OSSIM_DLL ossimKakaduMpiMasterOverviewSequencer : public ossimKakaduOverviewSequencer
{
public:

   /** default constructor */
   ossimKakaduMpiMasterOverviewSequencer();

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
    * Will allow you to get the next tile in the sequence.
    * Note the last tile returned will be an invalid
    * ossimRefPtr<ossimImageData>.  Callers should be able to do:
    *
    * ossimRefPtr<ossimImageData> id = sequencer->getNextTile();
    * while (id.valid())
    * {
    *    doSomething;
    *    id = sequencer->getNextTile();
    * }
    *
    */
   virtual bool getNextTile(std::ostream& bos);

protected:
   /** virtual destructor */
   virtual ~ossimKakaduMpiMasterOverviewSequencer();

   int  m_rank;
   int  m_numberOfProcessors;
   kd_core_local::kd_tlm_generator m_tlmGenerator;
   int m_tlmBytes;
   int m_maxTileParts;
};

#endif /* #ifndef ossimKakaduMpiMasterOverviewSequencer_HEADER */
