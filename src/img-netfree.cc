
#include <node.h>
#include <v8.h>

#include <node_buffer.h>
#include <node_object_wrap.h>

#include <FreeImage.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <math.h>

#include <iostream>
#include <map>


using namespace node;
using namespace v8;


struct Baton {
	uv_work_t request;
	Persistent<Function> callback;

	FIMEMORY* fiMemoryOut;
	FIMEMORY* fiMemoryIn;
	int count_pixel;
	int title_index;
};

std::map<int,FIBITMAP *> titles;


static BOOL Combine32(FIBITMAP *dst_dib, FIBITMAP *src_dib, unsigned x, unsigned y ) {
	// check the bit depth of src and dst images
	if((FreeImage_GetBPP(dst_dib) != 32) || (FreeImage_GetBPP(src_dib) != 32)) {
		return FALSE;
	}

	if(((int)x)<0 || ((int)y)<0){
		return FALSE;
	}
	// check the size of src image
	if((x + FreeImage_GetWidth(src_dib) > FreeImage_GetWidth(dst_dib)) || (y + FreeImage_GetHeight(src_dib) > FreeImage_GetHeight(dst_dib))) {
		return FALSE;
	}

	BYTE *dst_bits = FreeImage_GetBits(dst_dib) + ((FreeImage_GetHeight(dst_dib) - FreeImage_GetHeight(src_dib) - y) * FreeImage_GetPitch(dst_dib)) + (x * 4);
	BYTE *src_bits = FreeImage_GetBits(src_dib);

	unsigned alpha = 0;
	unsigned height = FreeImage_GetHeight(src_dib);
	unsigned line = FreeImage_GetLine(src_dib);

	for(unsigned rows = 0; rows < height; rows++) {
		for( unsigned cols = 0; cols < line; cols++ ){
#if 1		  
			alpha = src_bits[cols+3];
			if(alpha != 0){
			  dst_bits[cols] = (BYTE)(((src_bits[cols] - dst_bits[cols]) * alpha + (dst_bits[cols] << 8)) >> 8);
			  cols++;
			  dst_bits[cols] = (BYTE)(((src_bits[cols] - dst_bits[cols]) * alpha + (dst_bits[cols] << 8)) >> 8);
			  cols++;
			  dst_bits[cols] = (BYTE)(((src_bits[cols] - dst_bits[cols]) * alpha + (dst_bits[cols] << 8)) >> 8);
			  cols++;
			  if(alpha > dst_bits[cols]) dst_bits[cols] = alpha;
			}else
			  cols+=3;
#else
			alpha = src_bits[cols+3];
			if(alpha != 0){
			  dst_bits[cols] = (BYTE)((((0xff-dst_bits[cols]) - dst_bits[cols]) * alpha + (dst_bits[cols] << 8)) >> 8);
			  cols++;
			  dst_bits[cols] = (BYTE)((((0xff-dst_bits[cols]) - dst_bits[cols]) * alpha + (dst_bits[cols] << 8)) >> 8);
			  cols++;
			  dst_bits[cols] = (BYTE)((((0xff-dst_bits[cols]) - dst_bits[cols]) * alpha + (dst_bits[cols] << 8)) >> 8);
			  cols++;
			}else{
			  cols+=3;
			}
#endif
		}

		dst_bits += FreeImage_GetPitch(dst_dib);
		src_bits += FreeImage_GetPitch(src_dib);
	}

	return TRUE;
}

static void imageBlurWork(uv_work_t* req) {

	Baton* baton = static_cast<Baton*>(req->data);
	//uint i;
	//ImageThumb* obj = baton->obj;

	FIMEMORY * fiMemoryIn = NULL;
	FIMEMORY * fiMemoryOut = NULL;
	FIBITMAP * fiBitmap = NULL, *thumbnail1 = NULL, *thumbnail2 = NULL , *tmpImage = NULL;
	FREE_IMAGE_FORMAT format;
	int width , height , bpp;
	fiMemoryIn = baton->fiMemoryIn;	//FreeImage_OpenMemory((BYTE *)baton->imageBuffer,baton->imageBufferLength);
	std::map<int,FIBITMAP *>::iterator iter;
	int tw , th ;

	format = FreeImage_GetFileTypeFromMemory(fiMemoryIn);

	if (format < 0 /*|| FIF_GIF == format*/)
		goto ret;


	fiBitmap = FreeImage_LoadFromMemory(format, fiMemoryIn);

	if (!fiBitmap)
		goto ret;

//	FILTER_BOX		  = 0,	// Box, pulse, Fourier window, 1st order (constant) b-spline
//	FILTER_BICUBIC	  = 1,	// Mitchell & Netravali's two-param cubic filter
//	FILTER_BILINEAR   = 2,	// Bilinear filter
//	FILTER_BSPLINE	*  = 3,	// 4th order (cubic) b-spline
//	FILTER_CATMULLROM = 4,	// Catmull-Rom spline, Overhauser spline
//	FILTER_LANCZOS3	  = 5	// Lanczos3 filter


	width = FreeImage_GetWidth(fiBitmap);
	height = FreeImage_GetHeight(fiBitmap);

	bpp = FreeImage_GetBPP(fiBitmap);

	if(bpp != 32){
		tmpImage = FreeImage_ConvertTo32Bits(fiBitmap);
		FreeImage_Unload(fiBitmap);
		fiBitmap = tmpImage;
	}

	thumbnail1 = FreeImage_Rescale(fiBitmap, baton->count_pixel,baton->count_pixel, FILTER_BOX);

	if (!thumbnail1)
		goto ret;

	thumbnail2 = FreeImage_Rescale( thumbnail1, width, height, FILTER_BOX );


	if(baton->title_index){
		iter = titles.find(baton->title_index);
		if (iter != titles.end() )
		{
			if(iter->second){
				tw = FreeImage_GetWidth(iter->second);
				th = FreeImage_GetHeight(iter->second);
				Combine32( thumbnail2, iter->second, (width/2)-(tw/2) ,(height/2)-(th/2) );
			}
		}
	}

	if (!thumbnail2)
		goto ret;

	fiMemoryOut = FreeImage_OpenMemory();

	FreeImage_SaveToMemory( FIF_PNG, thumbnail2, fiMemoryOut, 0 );

	ret:

	if (fiMemoryIn)
		FreeImage_CloseMemory(fiMemoryIn);
	if (fiBitmap)
		FreeImage_Unload(fiBitmap);
	if (thumbnail1)
		FreeImage_Unload(thumbnail1);
	if(thumbnail2)
		FreeImage_Unload( thumbnail2 );

	baton->fiMemoryOut =  fiMemoryOut;

}

static void imageBlurAfter(uv_work_t* req) {
	HandleScope scope;
	Baton* baton = static_cast<Baton*>(req->data);

	if ( baton->fiMemoryOut ) {
		const unsigned argc = 2;
		const char*data;
		int datalen;
		FreeImage_AcquireMemory(baton->fiMemoryOut,(BYTE**)&data, (DWORD*)&datalen );
		Local<Value> argv[argc] = {
			Local<Value>::New( Null() ),
			Local<Object>::New( Buffer::New((const char*)data,datalen)->handle_)
		};

		TryCatch try_catch;
		baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
		if (try_catch.HasCaught())
			FatalException(try_catch);
	} else {
		Local < Value > err = Exception::Error(String::New("error"));

		const unsigned argc = 1;
		Local<Value> argv[argc] = {err};

		TryCatch try_catch;
		baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
		if (try_catch.HasCaught())
			FatalException(try_catch);
	}

	if (baton->fiMemoryOut)
		FreeImage_CloseMemory(baton->fiMemoryOut);


	baton->callback.Dispose();
	delete baton;
}

static Handle<Value> imageBlur(const Arguments& args) {
	HandleScope scope;

	Baton* baton = new Baton();
	baton->count_pixel = 3;
	baton->title_index = 0;

	int indexCallback = args.Length() - 1;

	if (args.Length() < 2)
		return ThrowException(Exception::TypeError(String::New("Expecting 2 arguments")));

	if (!Buffer::HasInstance(args[0]))
		return ThrowException( Exception::TypeError( String::New("First argument must be a Buffer")));

	Local < Function > callback;

	if( args.Length() > 2){
		if(args[1]->IntegerValue())
			baton->count_pixel = args[1]->IntegerValue();
	}

	if( args.Length() > 3 ){
		if(args[2]->IntegerValue())
			baton->title_index = args[2]->IntegerValue();
	}

	if (!args[indexCallback]->IsFunction())
		return ThrowException( Exception::TypeError( String::New( "no have callback")));

	callback = Local < Function > ::Cast(args[indexCallback]);

#if NODE_MAJOR_VERSION == 0 && NODE_MINOR_VERSION < 10
	Local < Object > buffer_obj = args[0]->ToObject();
#else
	Local<Value> buffer_obj = args[0];
#endif


	baton->request.data = baton;
	baton->callback = Persistent < Function > ::New(callback);


	baton->fiMemoryIn = FreeImage_OpenMemory((BYTE *) Buffer::Data(buffer_obj),
			Buffer::Length(buffer_obj));
	baton->fiMemoryOut = NULL;


	int status = uv_queue_work(uv_default_loop(), &baton->request,
			imageBlurWork, (uv_after_work_cb) imageBlurAfter);

	assert(status == 0);
	return Undefined();
}

static Handle<Value> addImageTitle(const Arguments& args) {
	HandleScope scope;

	int index = 0;
	FIBITMAP * imageTitle = NULL;

	if (args.Length() < 2)
		return ThrowException(Exception::TypeError(String::New("Expecting 2 arguments")));

	if (!Buffer::HasInstance(args[0]))
		return ThrowException( Exception::TypeError( String::New("First argument must be a Buffer")));

	Local < Function > callback;


	if(args[1]->IntegerValue())
	index = args[1]->IntegerValue();

	callback = Local < Function > ::Cast(args[2]);



#if NODE_MAJOR_VERSION == 0 && NODE_MINOR_VERSION < 10
	Local < Object > buffer_obj = args[0]->ToObject();
#else
	Local<Value> buffer_obj = args[0];
#endif



	FIMEMORY *  fiMemoryIn =  FreeImage_OpenMemory((BYTE *) Buffer::Data(buffer_obj), Buffer::Length(buffer_obj));

	imageTitle = FreeImage_LoadFromMemory(FreeImage_GetFileTypeFromMemory(fiMemoryIn), fiMemoryIn);

	if (fiMemoryIn)
		FreeImage_CloseMemory(fiMemoryIn);


	std::map<int,FIBITMAP *>::iterator iter = titles.find(index);
	if (iter != titles.end() ){
		if(iter->second) FreeImage_Unload( iter->second);
	}

	titles[index] = imageTitle;

	return Undefined();
}

extern "C" {
	void init(Handle<Object> target) {
		HandleScope scope;

		target->Set(String::NewSymbol("imageBlur"), FunctionTemplate::New(imageBlur)->GetFunction());
		target->Set(String::NewSymbol("addImageTitle"), FunctionTemplate::New(addImageTitle)->GetFunction());

	}

	NODE_MODULE(imgnetfree, init);
}
