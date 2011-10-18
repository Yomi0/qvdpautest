// kate: tab-indent on; indent-width 4; mixedindent off; indent-mode cstyle; remove-trailing-space on;
#include <QFile>
#include <QDataStream>

#include "mpegdecoder.h"

#define I_FRAME   1
#define P_FRAME   2
#define B_FRAME   3



MPEGDecoder::MPEGDecoder( VDPAUContext *v, QString filename )
{
	vc = v;
	decoder = VDP_INVALID_HANDLE;
	onlyDecode = false;
	testFile = filename;
}



MPEGDecoder::~MPEGDecoder()
{
	while ( !frames.isEmpty() )
		delete frames.takeFirst();
	if ( decoder != VDP_INVALID_HANDLE )
		vc->vdp_decoder_destroy( decoder );
	int i;
	for ( i=0; i<NUMSURFACES; ++i ) {
		if ( surfaces[i]!=VDP_INVALID_HANDLE )
			vc->vdp_video_surface_destroy( surfaces[i] );
	}
}



bool MPEGDecoder::init( bool decodeOnly )
{
	onlyDecode = decodeOnly;
	
	QFile file( testFile );
	if ( !file.open(QIODevice::ReadOnly) ) {
		fprintf( stderr, "%s", QString("MPEGDecoder: FATAL: Can't open %1 !!\n").arg(testFile).toLatin1().data() );
		return false;
	}
	
	QDataStream inData( &file );
	inData.readRawData( (char*)&width, 4 );
	inData.readRawData( (char*)&height, 4 );
	inData.readRawData( (char*)&ratio, 8 );
	inData.readRawData( (char*)&profile, 4 );
	int i;
	for ( i=0; i<FRAMESINSAMPLE; ++i ) {
		MPEGFrame *frame = new MPEGFrame();
		inData.readRawData( (char*)&frame->info, sizeof(frame->info) );
		inData.readRawData( (char*)&frame->dataLength, 4 );
		frame->data = new uint8_t[frame->dataLength];
		inData.readRawData( (char*)frame->data, frame->dataLength );
		frames.append( frame );
	}
	
	VdpStatus st = vc->vdp_decoder_create( vc->vdpDevice, profile, width, height, 2, &decoder );
	if ( st != VDP_STATUS_OK ) {
		fprintf( stderr, "MPEGDecoder: FATAL: Can't create decoder!!\n" );
		return false;
	}

	for ( i=0; i<NUMSURFACES; ++i ) {
		surfaces[ i ] = VDP_INVALID_HANDLE;
		if ( onlyDecode && i>2 )
			continue;
		st = vc->vdp_video_surface_create( vc->vdpDevice, VDP_CHROMA_TYPE_420, width, height, &surfaces[i] );
		if ( st != VDP_STATUS_OK ) {
			fprintf( stderr, "MPEGDecoder: FATAL: Can't create required surfaces!!\n" );
			return false;
		}
	}
	
	forwardRef = backwardRef = VDP_INVALID_HANDLE;
	currentSurface = surfaces[0];
	currentFrame = 0;
	
	//fprintf( stderr, "MPEGDecoder: profile = %d\n", profile );
	
	return true;
}



VdpVideoSurface MPEGDecoder::getNextFrame()
{
	MPEGFrame *frame = frames.at( currentFrame++ );
	
	frame->info.backward_reference = VDP_INVALID_HANDLE;
	frame->info.forward_reference = VDP_INVALID_HANDLE;
	if ( frame->info.picture_coding_type==P_FRAME )
		frame->info.forward_reference = backwardRef;
	else if ( frame->info.picture_coding_type==B_FRAME ) {
		frame->info.forward_reference = forwardRef;
		frame->info.backward_reference = backwardRef;
	}
	
	VdpBitstreamBuffer vbit;
	vbit.struct_version = VDP_BITSTREAM_BUFFER_VERSION;
	vbit.bitstream = frame->data;
	vbit.bitstream_bytes = frame->dataLength;
	VdpStatus st = vc->vdp_decoder_render( decoder, currentSurface, (VdpPictureInfo*)&frame->info, 1, &vbit );
	if ( st != VDP_STATUS_OK )
		fprintf( stderr, "MPEGDecoder: decoding failed!\n" );
		
	if ( frame->info.picture_coding_type!=B_FRAME ) {
		forwardRef = backwardRef;
		backwardRef = currentSurface;
	}
	VdpVideoSurface current = currentSurface;
	
	int i=0;
	currentSurface = surfaces[i];
	while ( (currentSurface==forwardRef || currentSurface==backwardRef) && i<3 )
		currentSurface = surfaces[++i];
		
	if ( currentFrame>=FRAMESINSAMPLE ) {
		forwardRef = backwardRef = VDP_INVALID_HANDLE;
		currentSurface = surfaces[0];
		currentFrame = 0;
	}
		
	return current;
}



QList< VdpVideoSurface > MPEGDecoder::getOrderedFrames()
{
	QList< VdpVideoSurface > list;
	int j;
	for ( j=0; j<NUMSURFACES; ++j ) {
		MPEGFrame *frame = frames.at( j );
	
		frame->info.backward_reference = VDP_INVALID_HANDLE;
		frame->info.forward_reference = VDP_INVALID_HANDLE;
		if ( frame->info.picture_coding_type==P_FRAME )
			frame->info.forward_reference = backwardRef;
		else if ( frame->info.picture_coding_type==B_FRAME ) {
			frame->info.forward_reference = forwardRef;
			frame->info.backward_reference = backwardRef;
		}

		VdpBitstreamBuffer vbit;
		vbit.struct_version = VDP_BITSTREAM_BUFFER_VERSION;
		vbit.bitstream = frame->data;
		vbit.bitstream_bytes = frame->dataLength;
		VdpStatus st = vc->vdp_decoder_render( decoder, surfaces[j], (VdpPictureInfo*)&frame->info, 1, &vbit );
		if ( st != VDP_STATUS_OK )
			fprintf( stderr, "MPEGDecoder: decoding failed: %s!\n", vc->vdp_get_error_string( st ) );

		
		if ( frame->info.picture_coding_type!=B_FRAME ) {				
			forwardRef = backwardRef;
			backwardRef = surfaces[j];
		}
	}
	
	QList< int > framesType;
	for ( j=0; j<NUMSURFACES; ++j ) {
		framesType.append( frames.at(j)->info.picture_coding_type );
		list.append( surfaces[j] );
	}

	j = 1;
	while ( j<NUMSURFACES ) {
		if ( framesType.at(j)==B_FRAME ) {
			framesType.swap(j-1, j);
			list.swap(j-1, j);
		}
		else
			++j;
	}
	
	vc->vdp_decoder_destroy( decoder );
	decoder = VDP_INVALID_HANDLE;
	
	return list;
}
