var fs = require('fs');
var gifDecode = require('../lib/index').decode;
var assert = require('assert');

console.log(gifDecode)
gifDecode(fs.readFileSync('./fourcolor.gif'), function(error, image) {
  assert(!error);
  assert.equal(image.width, 2);
  assert.equal(image.height, 2);
  assert.equal(image.colorAt(0,0), 0xff0000ff) //red
  assert.equal(image.colorAt(1,0), 0xff00ff00) //green
  assert.equal(image.colorAt(0,1), 0xffff0000) //blue
  assert.equal(image.colorAt(1,1), 0xffffffff) //white
});

gifDecode(new Buffer([1,2,3]), function(error, image) {
  //console.log(error);
  assert(error);
});