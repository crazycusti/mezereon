# Referenz-Header deiner Artefakte ansehen
hexdump -C stage2.bin | head -n1
hexdump -C stage3.bin | head -n1
hexdump -C kernel_payload.bin | head -n1

# Sektoren & erwartete LBAs berechnen
s2=$(( ( $(stat -c%s stage2.bin)  + 511 ) / 512 ))
s3=$(( ( $(stat -c%s stage3.bin)  + 511 ) / 512 ))
kS=$(( ( $(stat -c%s kernel_payload.bin) + 511 ) / 512 ))

S3_LBA=$((1 + s2))
K_LBA=$((S3_LBA + s3))

echo "Stage2=$s2 sec | Stage3=$s3 sec | Kernel=$kS sec"
echo "Expect: S3_LBA=$S3_LBA | K_LBA=$K_LBA"

# MBR-Signatur im Image checken (Stage1 @ LBA0)
printf "MBR signature: "; hexdump -v -s 510 -n 2 -e '2/1 "%02X\n"' disk.img   # sollte 55AA sein

# Stage2 im Image (soll bei LBA 1 starten)
dd if=disk.img bs=512 skip=1 count=1 | cmp -n 16 - stage2.bin \
  && echo "OK: Stage2 header @LBA1" || echo "MISMATCH: Stage2 @LBA1"

# Stage3 im Image an erwarteter Stelle
dd if=disk.img bs=512 skip=$S3_LBA count=1 | cmp -n 16 - stage3.bin \
  && echo "OK: Stage3 header @LBA=$S3_LBA" || echo "MISMATCH: Stage3 @LBA=$S3_LBA"

# Kernel im Image an erwarteter Stelle
dd if=disk.img bs=512 skip=$K_LBA count=1 | cmp -n 16 - kernel_payload.bin \
  && echo "OK: Kernel header @LBA=$K_LBA" || echo "MISMATCH: Kernel @LBA=$K_LBA"
