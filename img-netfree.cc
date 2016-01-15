
#include <nan.h>
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


using v8::FunctionTemplate;
using v8::Handle;
using v8::Object;
using v8::String;
using Nan::GetFunction;
using Nan::New;
using Nan::Set;

using namespace node;
using namespace v8;


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



class BlurWorker : public Nan::AsyncWorker {
 public:
  BlurWorker(Nan::Callback *callback) : Nan::AsyncWorker(callback) {}
  ~BlurWorker() {}

  // Executed inside the worker-thread.
  // It is not safe to access V8, or V8 data structures
  // here, so everything we need for input and output
  // should go on `this`.
  void Execute () {

	FIBITMAP * fiBitmap = NULL, *thumbnail1 = NULL, *thumbnail2 = NULL , *tmpImage = NULL;
	FREE_IMAGE_FORMAT format;
	int width , height , bpp;
	

	std::map<int,FIBITMAP *>::iterator iter;
	int tw , th ;
	
	fiMemoryIn = FreeImage_OpenMemory((BYTE *) imageBuffer, lengthBuffer);
	
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

	thumbnail1 = FreeImage_Rescale(fiBitmap, count_pixel,count_pixel, FILTER_BOX);

	if (!thumbnail1)
		goto ret;

	thumbnail2 = FreeImage_Rescale( thumbnail1, width, height, FILTER_BOX );


	if(title_index){
		iter = titles.find(title_index);
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


  }

  // Executed when the async work is complete
  // this function will be run inside the main event loop
  // so it is safe to use V8 again
  void HandleOKCallback () {
    
        Nan::HandleScope scope;


	if ( fiMemoryOut ) {
		const unsigned argc = 2;
		const char*data;
		int datalen;
		FreeImage_AcquireMemory(fiMemoryOut,(BYTE**)&data, (DWORD*)&datalen );
		Local<Value> argv[argc] = {
			Nan::Null(),
			Nan::CopyBuffer((char*)data,datalen).ToLocalChecked()
		};

		Nan::TryCatch try_catch;
		callback->Call(Nan::GetCurrentContext()->Global(), argc, argv);
		if (try_catch.HasCaught())
			Nan::FatalException(try_catch);
	} else {
		Local < Value > err = Exception::Error(New("error").ToLocalChecked());

		const unsigned argc = 1;
		Local<Value> argv[argc] = {err};

		Nan::TryCatch try_catch;
		callback->Call(Nan::GetCurrentContext()->Global(), argc, argv);
		if (try_catch.HasCaught())
			Nan::FatalException(try_catch);
	}

	if (fiMemoryOut)
		FreeImage_CloseMemory(fiMemoryOut);
	
	if(imageBuffer)
	  delete imageBuffer;
	
  }
  FIMEMORY* fiMemoryOut;
  FIMEMORY* fiMemoryIn;
  char *imageBuffer;
  size_t lengthBuffer;
  int count_pixel;
  int title_index;
  //private:

};


NAN_METHOD(imageBlur) {
	Nan::HandleScope scope;
	
	int indexCallback = info.Length() - 1;

	if (info.Length() < 2)
		return Nan::ThrowError("Expecting 2 arguments");

	if (!Buffer::HasInstance(info[0]))
		return Nan::ThrowError("First argument must be a Buffer");

	if (!info[indexCallback]->IsFunction())
		return Nan::ThrowError( "no have callback" );
	
	Nan::Callback *callback = new Nan::Callback(info[indexCallback].As<v8::Function>());
	
	BlurWorker* worker = new BlurWorker(callback);
	worker->count_pixel = 3;
	worker->title_index = 0;
	
	if( info.Length() > 2){
		if(info[1]->IntegerValue())
			worker->count_pixel = info[1]->IntegerValue();
	}

	if( info.Length() > 3 ){
		if(info[2]->IntegerValue())
			worker->title_index = info[2]->IntegerValue();
	}

	Local<Value> buffer_obj = info[0];
	
	worker->lengthBuffer = Buffer::Length(buffer_obj);
	worker->imageBuffer =  new char[worker->lengthBuffer];
	
	std::memcpy(worker->imageBuffer, Buffer::Data(buffer_obj) , worker->lengthBuffer);
	
	worker->fiMemoryIn = NULL;
	worker->fiMemoryOut = NULL;

	Nan::AsyncQueueWorker(worker);

}

NAN_METHOD(addImageTitle) {
	Nan::HandleScope scope;

	int index = 0;
	FIBITMAP * imageTitle = NULL;

	if (info.Length() < 2)
		return Nan::ThrowError("Expecting 2 arguments" );

	if (!Buffer::HasInstance(info[0]))
		return Nan::ThrowError("First argument must be a Buffer");


	if(info[1]->IntegerValue())
	index = info[1]->IntegerValue();

	

	Local<Value> buffer_obj = info[0];

	FIMEMORY *  fiMemoryIn =  FreeImage_OpenMemory((BYTE *) Buffer::Data(buffer_obj), Buffer::Length(buffer_obj));

	imageTitle = FreeImage_LoadFromMemory(FreeImage_GetFileTypeFromMemory(fiMemoryIn), fiMemoryIn);

	if (fiMemoryIn)
		FreeImage_CloseMemory(fiMemoryIn);


	std::map<int,FIBITMAP *>::iterator iter = titles.find(index);
	if (iter != titles.end() ){
		if(iter->second) FreeImage_Unload( iter->second);
	}

	titles[index] = imageTitle;

	info.GetReturnValue().SetUndefined();
}

NAN_MODULE_INIT(init) {
  
  Nan::Set(target, Nan::New<String>("imageBlur").ToLocalChecked(),
    GetFunction(Nan::New<FunctionTemplate>(imageBlur)).ToLocalChecked());

  Nan::Set(target, Nan::New<String>("addImageTitle").ToLocalChecked(),
    GetFunction(Nan::New<FunctionTemplate>(addImageTitle)).ToLocalChecked());
}

NODE_MODULE(imgnetfree, init)
