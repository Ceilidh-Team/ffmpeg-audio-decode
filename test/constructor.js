/* global describe, it */
'use strict'

require('should')
const Decoder = require('../').Decoder

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
function seek (_pos, _whence) { return -1 }
function length () { return emptyFlac.length }

describe('new Decoder', function () {
  it('should throw when given too few arguments', function () {
    (() => new Decoder()).should.throw();
    (() => new Decoder(null)).should.throw()
  })
  it('should throw when given non-function for close', function () {
    (() => new Decoder(null, read)).should.throw(/'close'/)
  })
  it('should throw when given non-function for read', function () {
    (() => new Decoder(close, null)).should.throw(/'read'/)
  })
  it('should throw when given non-function for close and read', function () {
    (() => new Decoder(null, null)).should.throw()
  })
  it('should throw when given a read function with insufficient arity', function () {
    (() => new Decoder(close, arity0)).should.throw(/'read'/)
  })
  it('should throw when given a seek function with insufficient arity', function () {
    (() => new Decoder(close, read, arity0, length)).should.throw(/'seek'/);
    (() => new Decoder(close, read, arity1, length)).should.throw(/'seek'/)
  })

  it('should not throw when given well-formed data', function () {
    (() => new Decoder(close, read)).should.not.throw();
    (() => new Decoder(close, read, seek, undefined)).should.not.throw();
    (() => new Decoder(close, read, undefined, length)).should.not.throw();
    (() => new Decoder(close, read, seek, length)).should.not.throw()
  })
  it('should throw when given malformed data', function () {
    (() => new Decoder(close, readMalformed)).should.throw();
    (() => new Decoder(close, readMalformed, undefined, length)).should.throw()
  })

  it('should be instanceof Decoder', function () {
    ((new Decoder(close, read)) instanceof Decoder).should.be.true()
  })
})
