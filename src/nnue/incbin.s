// Embed the default bullet-trained NNUE (akimbo "net.bin") directly into the
// binary so the engine is self-contained (no external EvalFile needed). The
// EvalFile UCI option can still override this at runtime.
#if defined(__APPLE__)
	.section __TEXT,__const
#else
	.section .rodata
#endif
	.global _CC_nnue_net
	.global _CC_nnue_net_end
	.balign 64
_CC_nnue_net:
	.incbin "src/nnue/net.bin"
_CC_nnue_net_end:
