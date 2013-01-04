var raw = require('../build/Release/node_gifblobber.node');

function BytePalettedImage(width, height, palette, pixels) {
  this.width = width;
  this.height = height;
  this.palette = palette;
  this.pixels = pixels;
}

BytePalettedImage.prototype.setPalette = function(index, colorRGBA) {
  this.palette.writeUInt32LE(colorRGBA, index*4);
}

BytePalettedImage.prototype.colorAt = function(x, y) {
  return this.palette.readUInt32LE(4 * this.pixels[x + y*this.width]);
}

module.exports = {
  decode: function(buffer, callback) {
    raw.decode(buffer, function(err, width, height, palette, pixels) {
      if (err) return callback(err);
      return callback(null, new BytePalettedImage(width, height, palette, pixels));
    });
  }
};