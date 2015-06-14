

var addon = require('./build/Release/imgnetfree');

module.exports = addon;

var fs = require('fs');

module.exports.addImageTitle(fs.readFileSync(__dirname + '/checking.png'), 1);
module.exports.addImageTitle(fs.readFileSync(__dirname + '/overflow.png'), 2);