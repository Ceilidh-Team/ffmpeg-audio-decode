'use strict'

const binding = require('bindings')('binding')

// TODO: Switch to typescript? Or at least write type definitions.

module.exports.createContext = function (close, read, seek, length) {
  if (typeof close !== 'function') {
    throw new TypeError('expected function for \'close\' argument')
  }
  if (typeof read !== 'function') {
    throw new TypeError('expected function for \'read\' argument')
  }
  if (typeof seek !== 'function' && typeof seek !== 'undefined') {
    throw new TypeError('expected function, null, or undefined for \'seek\' argument')
  }
  if (typeof length !== 'function' && typeof length !== 'undefined') {
    throw new TypeError('expected function, null, or undefined for \'length\' argument')
  }

  if (read.length < 1) {
    throw new TypeError('expected function taking >= 1 argument for \'read\' argument')
  }
  if (typeof seek === 'function') {
    if (seek.length < 2) {
      throw new TypeError('expected function taking >= 2 arguments for \'seek\' argument')
    }
  }

  return binding.createContext(close, read, seek, length)
}
