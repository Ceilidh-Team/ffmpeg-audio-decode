export function createContext(
  close: () => void,
  read: (buffer: Buffer) => number,
  seek?: (pos: number, whence: number) => number,
  length?: () => number
): any;
