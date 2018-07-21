/* global describe, it */
'use strict'

require('should')
const ffmpegAudioDecode = require('../')

function arity0 () {}
function arity1 (_a) {}

function close () {}
function read (buf) {
  console.log('js read|into ', buf, buf.length)
  buf.fill(0, buf.write('hello, world!'))
  console.log('js read|postwrite ', buf)
  return buf.length
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
    (() => ffmpegAudioDecode.createContext(close, read, arity0, length)).should.throw(/'seek'/)
    (() => ffmpegAudioDecode.createContext(close, read, arity1, length)).should.throw(/'seek'/)
  })
})

after(function () {
  // Forcibly garbage collect to ensure the hanging objects from these tests get cleaned.
  require('v8').setFlagsFromString('--expose-gc')
  require('vm').runInNewContext('gc')()
})
