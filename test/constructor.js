/* global describe, it */
'use strict'

require('should')
const Decode = require('../')
const Decoder = Decode.Decoder

class Decodeable {
  constructor (data) {
    this.pos = 0
    this.data = data
  }

  close () {
    this.pos = 0
    this.data = null
  }
  read (buffer) {
    let moved = buffer.write(this.data.substr(this.pos), 0, this.data.length - this.pos, 'ascii')
    if (moved === 0) {
      return undefined
    }

    this.pos += moved
    return moved
  }
  seek (pos, whence) {
    if (whence === 0) {
      this.pos = pos
    } else if (whence === 1) {
      this.pos += pos
    } else if (whence === 2) {
      this.pos = this.data.length - pos
    }

    return this.pos
  }
  length () {
    return this.data.length
  }
}

const emptyFlac = '\x66\x4c\x61\x43\x00\x00\x00\x22\x10\x00\x10\x00\xff\xff\xff\x00\x00\x00\x0a\xc4\x40\xf0\x00\x00\x00\x00\xd4\x1d\x8c\xd9\x8f\x00\xb2\x04\xe9\x80\x09\x98\xec\xf8\x42\x7e\x84\x00\x00\x28\x20\x00\x00\x00\x72\x65\x66\x65\x72\x65\x6e\x63\x65\x20\x6c\x69\x62\x46\x4c\x41\x43\x20\x31\x2e\x33\x2e\x30\x20\x32\x30\x31\x33\x30\x35\x32\x36\x00\x00\x00\x00'
class GoodDecodeable extends Decodeable {
  constructor () {
    super(emptyFlac)
  }
}
const malformedFlac = '\x67\x4d\x62\x44\x00\x00\x00\x22\x10\x00\x10\x00\xff\xff\xff\x00\x00\x00\x0a\xc4\x40\xf0\x00\x00\x00\x00\xd4\x1d\x8c\xd9\x8f\x00\xb2\x04\xe9\x80\x09\x98\xec\xf8\x42\x7e\x84\x00\x00\x28\x20\x00\x00\x00\x72\x65\x66\x65\x72\x65\x6e\x63\x65\x20\x6c\x69\x62\x46\x4c\x41\x43\x20\x31\x2e\x33\x2e\x30\x20\x32\x30\x31\x33\x30\x35\x32\x36\x00\x00\x00\x00'
class BadDecodeable extends Decodeable {
  constructor () {
    super(malformedFlac)
  }
}

describe('new Decoder', function () {
  it('should not throw when given well-formed data', function () {
    (() => new Decoder(new GoodDecodeable())).should.not.throw()
  })
  it('should throw when given malformed data', function () {
    (() => new Decoder(new BadDecodeable())).should.throw(Error)
  })

  it('should be instanceof Decoder', function () {
    new Decoder(new GoodDecodeable()).should.be.instanceof(Decoder)
  })
})
