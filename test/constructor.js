/* global describe, it */
'use strict'

require('should')
const Decode = require('../')
const Decoder = Decode.Decoder

class Decodable {
  constructor (data) {
    this.pos = 0
    this.data = data
  }

  close () {
    this.pos = 0
    this.data = null
  }
  read (buffer) {
    let toWrite = Math.min(this.data.length - this.pos, buffer.length)
    let moved = this.data.copy(buffer, 0, this.pos, this.pos + toWrite)
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

const emptyFlac = Buffer.from('ZkxhQwAAACIQABAA////AAAACsRA8AAAAADUHYzZjwCyBOmACZjs+EJ+hAAAKCAAAAByZWZlcmVuY2UgbGliRkxBQyAxLjMuMCAyMDEzMDUyNgAAAAA=', 'base64')
const malformedFlac = Buffer.from('Z01iRAAAACIQABAA////AAAACsRA8AAAAADUHYzZjwCyBOmACZjs+EJ+hAAAKCAAAAByZWZlcmVuY2UgbGliRkxBQyAxLjMuMCAyMDEzMDUyNgAAAAA=', 'base64')

describe('new Decoder', function () {
  it('should not throw when given well-formed data', function () {
    (() => new Decoder(new Decodable(emptyFlac))).should.not.throw()
  })
  it('should throw when given malformed data', function () {
    (() => new Decoder(new Decodable(malformedFlac))).should.throw(Error)
  })

  it('should be instanceof Decoder', function () {
    new Decoder(new Decodable(emptyFlac)).should.be.instanceof(Decoder)
  })
})
