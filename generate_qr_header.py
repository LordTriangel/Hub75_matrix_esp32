import qrcode
import sys
import math
import os

url = "https://tinyurl.com/agripvHM"
output_file = "src/qrcode_bitmap.h"
SCALE = 2 # 2x Zoom

print(f"Generating QR code for: {url}")
print(f"Scale Factor: {SCALE}")

# Create QR Code
qr = qrcode.QRCode(
    version=1,
    error_correction=qrcode.constants.ERROR_CORRECT_L,
    box_size=1, # Base size, we scale later manually for GFX
    border=1,   # Minimal border
)
qr.add_data(url)
qr.make(fit=True)

matrix_base = qr.get_matrix()
base_width = len(matrix_base[0])
base_height = len(matrix_base)

print(f"Base QR Size: {base_width}x{base_height} modules")

# Scale Logic
width = base_width * SCALE
height = base_height * SCALE

print(f"Output Bitmap Size: {width}x{height}")

if width > 64 or height > 64:
    print(f"Error: QR code too big ({width}x{height} > 64x64)")
    sys.exit(1)

# Generate C Code
c_code = []
c_code.append(f"// Generated QR Code Bitmap for {url}")
c_code.append(f"// Size: {width}x{height} (Scaled x{SCALE})")
c_code.append("#ifndef QRCODE_BITMAP_H")
c_code.append("#define QRCODE_BITMAP_H")
c_code.append("")
c_code.append("#include <Arduino.h>")
c_code.append("")
c_code.append(f"#define QR_WIDTH {width}")
c_code.append(f"#define QR_HEIGHT {height}")
c_code.append("")
c_code.append("// Bitmap data for Adafruit_GFX (MSB first, horizontal scan)")
c_code.append(f"const uint8_t qrcode_bitmap[] PROGMEM = {{")

byte_width = math.ceil(width / 8.0)

for y in range(height):
    base_y = y // SCALE
    row_bytes = []
    current_byte = 0
    bit_count = 0
    
    for x in range(width):
        base_x = x // SCALE
        bit = 1 if matrix_base[base_y][base_x] else 0 
        
        if bit:
             current_byte |= (1 << (7 - bit_count))
             
        bit_count += 1
        
        if bit_count == 8:
            row_bytes.append(f"0x{current_byte:02X}")
            current_byte = 0
            bit_count = 0
            
    if bit_count > 0:
        row_bytes.append(f"0x{current_byte:02X}")
        
    c_code.append("  " + ", ".join(row_bytes) + ",")

c_code.append("};")
c_code.append("")
c_code.append("#endif // QRCODE_BITMAP_H")

with open(output_file, "w") as f:
    f.write("\n".join(c_code))

print(f"Successfully generated {output_file}")
