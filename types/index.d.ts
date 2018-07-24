// Set from the beginning, offset from current, and set from end
declare type Whence = 0 | 1 | 2;

declare interface IDecodeable {
  close(): void;
  read(buffer: Buffer): number | undefined;
  seek?(pos: number, whence: Whence): number;
  length?(): number;
}

declare class Decoder {
  constructor(subject: IDecodeable);

  public read(into: Buffer): void;
}
