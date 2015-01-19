
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


using namespace node;
using namespace v8;


struct Baton {
	uv_work_t request;
	Persistent<Function> callback;

	FIMEMORY* fiMemoryOut;
	FIMEMORY* fiMemoryIn;
	int count_pixel;
};

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

	if(bpp != 32 || bpp != 24){
		tmpImage = FreeImage_ConvertTo32Bits(fiBitmap);
		FreeImage_Unload(fiBitmap);
		fiBitmap = tmpImage;
	}

	thumbnail1 = FreeImage_Rescale(fiBitmap, baton->count_pixel,baton->count_pixel, FILTER_BOX);

	if (!thumbnail1)
		goto ret;

	thumbnail2 = FreeImage_Rescale( thumbnail1, width, height, FILTER_BOX );

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

	int count_pixel = 3;

	if (args.Length() < 2)
		return ThrowException(Exception::TypeError(String::New("Expecting 2 arguments")));

	if (!Buffer::HasInstance(args[0]))
		return ThrowException( Exception::TypeError( String::New("First argument must be a Buffer")));

	Local < Function > callback;

	if( args.Length() > 2){
		if (!args[2]->IsFunction())
				return ThrowException( Exception::TypeError( String::New( "3 argument must be a function")));

		if(args[1]->IntegerValue())
			count_pixel = args[1]->IntegerValue();

		callback = Local < Function > ::Cast(args[2]);
	}else{
		if (!args[1]->IsFunction())
				return ThrowException( Exception::TypeError( String::New( "2 argument must be a function")));

		callback = Local < Function > ::Cast(args[1]);
	}


#if NODE_MAJOR_VERSION == 0 && NODE_MINOR_VERSION < 10
	Local < Object > buffer_obj = args[0]->ToObject();
#else
	Local<Value> buffer_obj = args[0];
#endif

	Baton* baton = new Baton();
	baton->request.data = baton;
	baton->callback = Persistent < Function > ::New(callback);


	baton->fiMemoryIn = FreeImage_OpenMemory((BYTE *) Buffer::Data(buffer_obj),
			Buffer::Length(buffer_obj));
	baton->fiMemoryOut = NULL;
	baton->count_pixel = count_pixel;

	int status = uv_queue_work(uv_default_loop(), &baton->request,
			imageBlurWork, (uv_after_work_cb) imageBlurAfter);

	assert(status == 0);
	return Undefined();
}



extern "C" {
	void init(Handle<Object> target) {
		HandleScope scope;

		target->Set(String::NewSymbol("imageBlur"), FunctionTemplate::New(imageBlur)->GetFunction());

	}

	NODE_MODULE(imgnetfree, init);
}
