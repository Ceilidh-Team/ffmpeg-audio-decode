declare class AvContext {
  constructor(
    close: () => void,
    read: (buffer: Buffer) => number,
    seek?: (pos: number, whence: number) => number,
    length?: () => number
  );
}
