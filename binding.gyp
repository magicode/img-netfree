{
    "targets": [
    {
       "target_name": "imgnetfree",
       "sources": [ 
            'img-netfree.cc'
       ],
       "include_dirs" : [
	    "<!(node -e \"require('nan')\")",
	    "<!(node -e \"require('free-image-lib/include_dirs')\")"
       ],
       "libraries": [
	    "../<!(node -e \"require('free-image-lib/libraries')\")"
       ]
    }
  ]
}

