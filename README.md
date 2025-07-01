# Blot

A compiler-explorer clone, but works with your toolchain and your project.

Very embryonic for now, but one of the goals is for this to be a
central building block in :

* source code editor plugins (either as linked-in library or as a
  subprocess) to get live disassembly of your sources as you edit your
  code in your project.  Not unlike the Emacs plugin
  https://github.com/joaotavora/beardbolt, but much faster and more
  powerful.
  
* project browsers within code forges.


## Some basic usage:

```
BUILD_TYPE=Debug                                        &&
BUILD_DIR=build-$BUILD_TYPE                             &&
cmake -B $BUILD_DIR -DCMAKE_BUILD_TYPE=$BUILD_TYPE      &&
cmake --build $BUILD_DIR -j                             &&
$BUILD_DIR/blot --debug=3 test/test01.cpp
```

This should produce (among log lines and etc) some assembly output not
far from what Compiler Explorer shows for the same snippet. 

```
main:
	pushq	%rbp
	movl	$26, %edi
	pushq	%rbx
	subq	$24, %rsp
	call	_Znwm@PLT
	movdqa	.LC0(%rip), %xmm0
	movl	$25, %edx
	leaq	_ZSt4cout(%rip), %rdi
	movb	$0, 25(%rax)
	movq	%rax, %rsi
	movq	%rax, %rbx
	movups	%xmm0, (%rax)
	movdqa	.LC1(%rip), %xmm0
	movups	%xmm0, 9(%rax)
	call	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l@PLT
	movq	%rax, %rbp
	movq	(%rax), %rax
	movq	-24(%rax), %rax
	movq	240(%rbp,%rax), %rdi
	testq	%rdi, %rdi
	je	.L12
	cmpb	$0, 56(%rdi)
	je	.L5
	movsbl	67(%rdi), %esi
.L6:
	movq	%rbp, %rdi
	call	_ZNSo3putEc@PLT
	movq	%rax, %rdi
	call	_ZNSo5flushEv@PLT
	movq	%rbx, %rdi
	movl	$26, %esi
	call	_ZdlPvm@PLT
	addq	$24, %rsp
	xorl	%eax, %eax
	popq	%rbx
	popq	%rbp
	ret
.L5:
	movq	%rdi, 8(%rsp)
	call	_ZNKSt5ctypeIcE13_M_widen_initEv@PLT
	movq	8(%rsp), %rdi
	leaq	_ZNKSt5ctypeIcE8do_widenEc(%rip), %rdx
	movl	$10, %esi
	movq	(%rdi), %rax
	movq	48(%rax), %rax
	cmpq	%rdx, %rax
	je	.L6
	movl	$10, %esi
	call	*%rax
	movsbl	%al, %esi
	jmp	.L6
	jmp	.L11
.L12:
	call	_ZSt16__throw_bad_castv@PLT
.L11:
	movq	%rax, %rbp
	movq	%rbx, %rdi
	movl	$26, %esi
	call	_ZdlPvm@PLT
	movq	%rbp, %rdi
	call	_Unwind_Resume@PLT
.LC0:
	.quad	4836914856867947848
	.quad	2338042672858557807
.LC1:
	.quad	4981106967807881325
	.quad	2410100292766691448
```
