{  
  'variables': {
      'makefreeimage%': '<!(./deps/makefreeimage.sh)',
   },
  "targets": [
    {
       "target_name": "imgnetfree",
       "sources": [ 
            'img-netfree.cc'
       ],
       "include_dirs" : [
	    "<!(node -e \"require('nan')\")"
       ],
       'conditions': [
		    ['makefreeimage=="true"', {
		      'include_dirs':  [ "../deps/FreeImage/Dist/FreeImage.h" ],
		      "libraries": [ "../deps/FreeImage/Dist/libfreeimage.a"]
		    }, {
		      "libraries": ['-lfreeimage']
		    }]
		],
    }
    
  ]
}

