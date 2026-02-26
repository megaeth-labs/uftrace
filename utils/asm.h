#ifndef MOTRACE_ASM_H
#define MOTRACE_ASM_H

/* clang-format off */

#define GLOBAL(sym)				\
	.global sym;				\
	.type sym, %function;			\
sym:						\
	.global motrace_ ## sym;		\
	.hidden motrace_ ## sym;		\
	.type motrace_ ## sym, %function;	\
motrace_ ## sym:

#define ENTRY(sym)				\
	.global sym;				\
	.hidden sym;				\
	.type sym, %function;			\
sym:

#define END(sym)				\
	.size sym, .-sym;

/* clang-format on */

#endif /* MOTRACE_ASM_H */
