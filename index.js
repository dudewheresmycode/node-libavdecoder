
var Decoder = require('./build/Release/avdecoder').Emitter;
var EventEmitter = require('events').EventEmitter;
var util = require('util');

util.inherits(Decoder, EventEmitter);
module.exports = Decoder;
