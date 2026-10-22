import struct
import os

def create_sample_pe(filepath):
    dos_header = bytearray(64)
    struct.pack_into("<H", dos_header, 0, 0x5A4D) # MZ
    struct.pack_into("<I", dos_header, 60, 0x80)  # e_lfanew -> 0x80

    dos_stub = b"This program cannot be run in DOS mode.\r\r\n$\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    dos_stub = dos_stub.ljust(64, b"\0")

    signature = b"PE\0\0"

    file_header = struct.pack(
        "<HHIIIHH",
        0x014C,    # Machine: i386
        1,         # NumberOfSections
        0x67170000,# TimeDateStamp
        0,         # PointerToSymbolTable
        0,         # NumberOfSymbols
        224,       # SizeOfOptionalHeader
        0x0102     # Characteristics: 32BIT_MACHINE | EXECUTABLE_IMAGE
    )

    optional_header = bytearray(224)
    struct.pack_into("<H", optional_header, 0, 0x010B)       # PE32
    struct.pack_into("<I", optional_header, 16, 0x1000)      # AddressOfEntryPoint
    struct.pack_into("<I", optional_header, 28, 0x00400000)  # ImageBase
    struct.pack_into("<I", optional_header, 32, 0x1000)      # SectionAlignment
    struct.pack_into("<I", optional_header, 36, 0x0200)      # FileAlignment
    struct.pack_into("<H", optional_header, 48, 4)           # MajorSubsystemVersion
    struct.pack_into("<H", optional_header, 68, 2)           # Subsystem: Windows GUI
    struct.pack_into("<I", optional_header, 56, 0x2000)      # SizeOfImage
    struct.pack_into("<I", optional_header, 60, 0x0200)      # SizeOfHeaders
    struct.pack_into("<I", optional_header, 92, 16)          # NumberOfRvaAndSizes

    section_header = bytearray(40)
    section_header[0:5] = b".text"
    struct.pack_into("<I", section_header, 8, 0x0100)        # VirtualSize
    struct.pack_into("<I", section_header, 12, 0x1000)       # VirtualAddress
    struct.pack_into("<I", section_header, 16, 0x0200)       # SizeOfRawData
    struct.pack_into("<I", section_header, 20, 0x0200)       # PointerToRawData
    struct.pack_into("<I", section_header, 36, 0x60000020)   # Characteristics: CODE | EXECUTE | READ

    headers = dos_header + dos_stub + signature + file_header + bytes(optional_header) + bytes(section_header)
    headers = headers.ljust(512, b"\0")

    # Code: xor eax, eax; ret
    code = b"\x31\xc0\xc3".ljust(512, b"\0")

    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    with open(filepath, "wb") as f:
        f.write(headers + code)

if __name__ == "__main__":
    create_sample_pe("rootfs/hello.exe")
    create_sample_pe("rootfs/app.exe")
    print("Generated PE32 Windows executables for FishWine.")
