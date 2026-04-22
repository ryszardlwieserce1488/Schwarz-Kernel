section .rodata

global _binary_consolas_ttf_start
global _binary_consolas_ttf_size
_binary_consolas_ttf_start:
    incbin "consolas.ttf"
_binary_consolas_ttf_end:
_binary_consolas_ttf_size:
    dq _binary_consolas_ttf_end - _binary_consolas_ttf_start

global _binary_inconsolata_ttf_start
global _binary_inconsolata_ttf_size
_binary_inconsolata_ttf_start:
    incbin "inconsolata.ttf"
_binary_inconsolata_ttf_end:
_binary_inconsolata_ttf_size:
    dq _binary_inconsolata_ttf_end - _binary_inconsolata_ttf_start

global _binary_segoeuithis_ttf_start
global _binary_segoeuithis_ttf_size
_binary_segoeuithis_ttf_start:
    incbin "segoeuithis.ttf"
_binary_segoeuithis_ttf_end:
_binary_segoeuithis_ttf_size:
    dq _binary_segoeuithis_ttf_end - _binary_segoeuithis_ttf_start

global _binary_times_ttf_start
global _binary_times_ttf_size
_binary_times_ttf_start:
    incbin "times.ttf"
_binary_times_ttf_end:
_binary_times_ttf_size:
    dq _binary_times_ttf_end - _binary_times_ttf_start

