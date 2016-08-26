//*******************************************************************
// Copyright (C) 2000 ImageLinks Inc.
//
// License:  LGPL
//
// See LICENSE.txt file in the top level directory for more details.
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
//  $Id: ossimKakaduImageMpiMWriterSequenceConnection.h 9094 2006-06-13 19:12:40Z dburken $
//  RP - modified for JPEG2000
//
#ifndef ossimKakaduImageMpiMWriterSequenceConnection_HEADER
#define ossimKakaduImageMpiMWriterSequenceConnection_HEADER
#include <ossim/imaging/ossimImageSourceSequencer.h>
#include <kdu_compressed.h>
#include <compressed_local.h>
#include "ossimKakaduCompressedTarget.h"
//class ossimImageData;
class ossimKakaduImageMpiMWriterSequenceConnection : public ossimImageSourceSequencer
{
public:
   ossimKakaduImageMpiMWriterSequenceConnection(ossimObject* owner=NULL);
   ossimKakaduImageMpiMWriterSequenceConnection(ossimImageSource* inputSource,
                                          ossimObject* owner=NULL);
   virtual ~ossimKakaduImageMpiMWriterSequenceConnection();

   /*!
    * This is a virtual method that can be overriden
    * by derived classes (MPI, or PVM or other
    * parallel connections.  This method is to indicate
    * whether or not this connection is the master/
    * controlling connection.
    */
   virtual void initialize();
   virtual void setToStartOfSequence();
   virtual bool getNextTileStream(std::ostream& bos);

   virtual bool isMaster()const
      {
         return true;
      }
   /*!
    * Will allow you to get the next tile in the sequence.
    */
   virtual ossimRefPtr<ossimImageData> getNextTile(ossim_uint32 resLevel=0);
protected:
   int theNumberOfProcessors;
   int theRank;
   bool theNeedToSendRequest;
   kd_core_local::kd_tlm_generator m_tlmGenerator;
   int m_tlmBytes;
   int m_maxTileParts;

TYPE_DATA
};

#endif
