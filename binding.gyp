{  
  'variables': {
      'makefreeimage%': '<!(./deps/makefreeimage.sh)',
   },
  "targets": [
    {
       "target_name": "imgnetfree",
       "sources": [ 
            'src/img-netfree.cc'
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

