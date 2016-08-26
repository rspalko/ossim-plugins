//*******************************************************************
// Copyright (C) 2000 ImageLinks Inc.
//
// License:  LGPL
//
// See LICENSE.txt file in the top level directory for more details.
//
// Author:  Garrett Potts
//
//*******************************************************************
//  $Id: ossimKakaduImageMpiSWriterSequenceConnection.h 9094 2006-06-13 19:12:40Z dburken $
//  RP - modified for JPEG2000
#ifndef ossimKakaduImageMpiSWriterSequenceConnection_HEADER
#define ossimKakaduImageMpiSWriterSequenceConnection_HEADER
#include <ossim/imaging/ossimImageSourceSequencer.h>
#include "ossimKakaduCompressor.h"
#include <compressed_local.h>
class ossimImageData;
class ossimKakaduImageMpiSWriterSequenceConnection : public ossimImageSourceSequencer
{
public:
   ossimKakaduImageMpiSWriterSequenceConnection(ossimObject* owner=NULL,
                                          long numberOfTilesToBuffer = 2);

   ossimKakaduImageMpiSWriterSequenceConnection(ossimImageSource* inputSource,
                                          ossimObject* owner=NULL,
                                          long numberOfTilesToBuffer = 2);

   virtual ~ossimKakaduImageMpiSWriterSequenceConnection();
   virtual bool isMaster()const
      {
         return false;
      }

   virtual void initialize();
   virtual void setToStartOfSequence();
   /*!
    * Will allow you to get the next tile in the sequence.
    */
   virtual ossimRefPtr<ossimImageData> getNextTile(ossim_uint32 resLevel=0);

   virtual void slaveProcessTiles();
   void setProgressionOrder(const ossimString& corder) const;

protected:
   int theNumberOfProcessors;
   int theRank;
   int theNumberOfTilesToBuffer;

   //ossimRefPtr<ossimImageData>* theOutputTile;

   void deleteOutputTiles();
   ossimKakaduCompressor*         m_compressor;

TYPE_DATA
};

#endif
