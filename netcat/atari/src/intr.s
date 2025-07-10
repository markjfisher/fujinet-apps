	.export _ih
	.import _trip
	.import _ih_count

_ih:
	INC _ih_count
	LDA #$01
	STA _trip
	PLA
	RTI
