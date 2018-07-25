/** Set from the beginning, offset from current, and set from end */
declare type Whence = 0 | 1 | 2

/** A stream that can be decoded by FFmpeg */
declare interface Decodeable {
  /** Close the stream, indicating that no more reading will be performed */
  close (): void
  /**
   * Read data from the stream into a buffer, returning the number of bytes read or `undefined` on EOF.
   * @param buffer The buffer to write to. Attempts to fill the entire buffer, starting from the beginning.
   */
  read (buffer: Buffer): number | undefined
  /**
   * Seek the stream to the specified position
   * @param offset The offset to use when seeking
   * @param whence The method for computing the final position from the offset
   */
  seek? (offset: number, whence: Whence): number
  /** The total length of this stream, in bytes */
  length? (): number
}

/** An object capable of decoding a stream using FFmpeg */
export class Decoder {
  /**
   * Construct a new Decoder from a given stream
   * @param subject The stream to decode
   */
  constructor (subject: Decodeable)

  /**
   * Read audio data from the decoder into a buffer
   * @param into The buffer to read into
   */
  read (into: Buffer): void
}
