/** Set from the beginning, offset from current, and set from end */
declare type Whence = 0 | 1 | 2

/** A stream that can be decoded by FFmpeg */
declare interface Decodable {
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
   * @param decodable object to decode
   */
  constructor (decodable: Decodable)

  /**
   * Whether seek is legal to call
   */
  canSeek: boolean

  /**
   * The number of audio streams
   */
  streamCount: number

  /**
   * Read all metadata
   * @returns A map from key to value of tags
   */
  metadata (): Map<string, string>

  /**
   * Read a single tag of metadata
   * @param tag tag, for example 'title'
   * @returns {string} value of the tag
   * @returns {undefined} tag is not present
   */
  metadata (tag: string): string | undefined

  /**
   * Read audio data into a buffer
   * @returns {Buffer} data is interleaved
   * @returns {Buffer[]} data is in separate planes
   * @returns {undefined} end of file
   */
  read (): Buffer | Buffer[] | undefined

  /**
   * Seek to a timestamp
   * @throws {Error} canSeek is false
   */
  seek (seconds: number): void

  /**
   * Select a stream
   * @throws {Error} id must be less than streamCount
   */
  selectStream (id: number): void
}
