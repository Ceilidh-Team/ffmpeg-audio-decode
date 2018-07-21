/* global describe, it, after */
'use strict'

require('should')
const ffmpegAudioDecode = require('../')

const emptyFlac = '\x66\x4c\x61\x43\x00\x00\x00\x22\x10\x00\x10\x00\xff\xff\xff\x00\x00\x00\x0a\xc4\x40\xf0\x00\x00\x00\x00\xd4\x1d\x8c\xd9\x8f\x00\xb2\x04\xe9\x80\x09\x98\xec\xf8\x42\x7e\x84\x00\x00\x28\x20\x00\x00\x00\x72\x65\x66\x65\x72\x65\x6e\x63\x65\x20\x6c\x69\x62\x46\x4c\x41\x43\x20\x31\x2e\x33\x2e\x30\x20\x32\x30\x31\x33\x30\x35\x32\x36\x00\x00\x00\x00'
const malformedFlac = '\x67\x4d\x62\x44\x00\x00\x00\x22\x10\x00\x10\x00\xff\xff\xff\x00\x00\x00\x0a\xc4\x40\xf0\x00\x00\x00\x00\xd4\x1d\x8c\xd9\x8f\x00\xb2\x04\xe9\x80\x09\x98\xec\xf8\x42\x7e\x84\x00\x00\x28\x20\x00\x00\x00\x72\x65\x66\x65\x72\x65\x6e\x63\x65\x20\x6c\x69\x62\x46\x4c\x41\x43\x20\x31\x2e\x33\x2e\x30\x20\x32\x30\x31\x33\x30\x35\x32\x36\x00\x00\x00\x00'

function arity0 () {}
function arity1 (_a) {}

function close () {}
function read (buf) {
  return buf.write(emptyFlac, 0, emptyFlac.length, 'ascii')
}
function readMalformed (buf) {
  return buf.write(malformedFlac, 0, malformedFlac.length, 'ascii')
}
function seek (pos, whence) { return -1 }
function length () { return -1 }

describe('createContext', function () {
  it('should throw when given non-function for close', function () {
    (() => ffmpegAudioDecode.createContext(null, read)).should.throw(/'close'/)
  })
  it('should throw when given non-function for read', function () {
    (() => ffmpegAudioDecode.createContext(close, null)).should.throw(/'read'/)
  })
  it('should throw when given non-function for close and read', function () {
    (() => ffmpegAudioDecode.createContext(null, null)).should.throw(TypeError)
  })

  it('should not throw when given appropriate functions for close and read', function () {
    (() => ffmpegAudioDecode.createContext(close, read)).should.not.throw()
  })
  it('should throw when given a read function with insufficient arity', function () {
    (() => ffmpegAudioDecode.createContext(close, arity0)).should.throw(/'read'/)
  })

  it('should not throw when given appropriate functions for seek and length', function () {
    (() => ffmpegAudioDecode.createContext(close, read, seek, length)).should.not.throw()
  })
  it('should not throw for undefined in arguments 3 and 4', function () {
    (function () {
      ffmpegAudioDecode.createContext(close, read, undefined, length)
      ffmpegAudioDecode.createContext(close, read, seek, undefined)
    }).should.not.throw()
  })
  it('should not throw for given seek and missing length or vice versa', function () {
    (function () {
      ffmpegAudioDecode.createContext(close, read, undefined, length)
      ffmpegAudioDecode.createContext(close, read, seek, undefined)
    }).should.not.throw()
  })
  it('should throw when given a seek function with insufficient arity', function () {
    (() => ffmpegAudioDecode.createContext(close, read, arity0, length)).should.throw(/'seek'/);
    (() => ffmpegAudioDecode.createContext(close, read, arity1, length)).should.throw(/'seek'/)
  })

  it('should throw when given malformed data', function () {
    (() => ffmpegAudioDecode.createContext(close, readMalformed)).should.throw()
  })
})

after(function () {
  // Forcibly garbage collect to ensure the hanging objects from these tests get cleaned.
  require('v8').setFlagsFromString('--expose-gc')
  require('vm').runInNewContext('gc')()
})
