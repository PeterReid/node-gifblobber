var raw = require('../build/Release/node_gifblobber.node');

function BytePalettedImage(width, height, pixels, unfiltered_palette, filtered_palette) {
  this.width = width;
  this.height = height;
  this.pixels = pixels;
  this.unfiltered_palette = unfiltered_palette;
  this.filtered_palette = filtered_palette;
}

BytePalettedImage.prototype.setPalette = function(index, colorRGBA) {
  this.palette.writeUInt32LE(colorRGBA, index*4);
}

BytePalettedImage.prototype.colorAt = function(x, y) {
  return this.palette.readUInt32LE(4 * this.pixels[x + y*this.width]);
}

BytePalettedImage.prototype.stretch = function(sourceLeft, sourceRight, sourceTop, sourceBottom, width, height, filtered, dest, cb) {
  raw.stretch(this.pixels, this.width, this.height, this.unfiltered_palette, this.filtered_palette, sourceLeft, sourceRight, sourceTop, sourceBottom, width, height, !!filtered, dest, cb);
}


module.exports = {
  decode: function(buffer, callback) {
    raw.slurp(buffer, function(err, width, height, pixels, unfiltered_palette, filtered_palette) {
      if (err) return callback(err);
      return callback(null, new BytePalettedImage(width, height, pixels, unfiltered_palette, filtered_palette));
    });
  },
  raw: raw,
};
