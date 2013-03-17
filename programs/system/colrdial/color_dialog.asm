;*****************************************************************************
; Color Dialog - for Kolibri OS
; Copyright (c) 2013, Marat Zakiyanov aka Mario79, aka Mario
; All rights reserved.
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions are met:
;        * Redistributions of source code must retain the above copyright
;          notice, this list of conditions and the following disclaimer.
;        * Redistributions in binary form must reproduce the above copyright
;          notice, this list of conditions and the following disclaimer in the
;          documentation and/or other materials provided with the distribution.
;        * Neither the name of the <organization> nor the
;          names of its contributors may be used to endorse or promote products
;          derived from this software without specific prior written permission.
;
; THIS SOFTWARE IS PROVIDED BY Marat Zakiyanov ''AS IS'' AND ANY
; EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
; WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
; DISCLAIMED. IN NO EVENT SHALL <copyright holder> BE LIABLE FOR ANY
; DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
; (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
; ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
; (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
; SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;*****************************************************************************
  use32
  org	 0x0

  db	 'MENUET01'
  dd	 0x01
  dd	 START
  dd	 IM_END
  dd	 I_END
  dd	 stacktop
  dd	 0x0
  dd	 0x0
;---------------------------------------------------------------------
include '../../macros.inc'
;include 'lang.inc'
;---------------------------------------------------------------------
p_start_x = 10
p_start_y = 10

p_size_x = 20
p_size_y = 256
;--------------------------------------
t_start_x = 40
t_start_y = 10
;--------------------------------------
w_start_x = 200
w_start_y = 200

w_size_x = 400
w_size_y = 350
;--------------------------------------
c_start_x = t_start_x + p_size_y + 10
c_start_y = 10

c_size_x = 40
c_size_y = 20
;---------------------------------------------------------------------

START:
	mcall	68,11

	xor	eax,eax
	mov	al,p_size_x
	mov	[palette_SIZE_X],eax
	mov	ax,p_size_y
	mov	[palette_SIZE_Y],eax
	mov	[tone_SIZE_X],eax
	mov	[tone_SIZE_Y],eax
	mov	eax,0xff0000
	mov	[tone_color],eax
	mov	[selected_color],eax
;--------------------------------------
	mov	ecx,[palette_SIZE_Y]
	imul	ecx,[palette_SIZE_X]
	lea	ecx,[ecx*3]
	inc	ecx	;reserve for stosd
	mcall	68,12
	mov	[palette_area],eax
;--------------------------------------
	call	create_palette
;--------------------------------------	
	mov	ecx,[tone_SIZE_Y]
	imul	ecx,[tone_SIZE_X]
	lea	ecx,[ecx*3]
	inc	ecx	;reserve for stosd
	mcall	68,12
	mov	[tone_area],eax
;--------------------------------------
	call    create_tone
;---------------------------------------------------------------------
align 4
red:
	call	draw_window
;---------------------------------------------------------------------
align 4
still:
	mcall	10

	cmp	eax,1
	je	red

	cmp	eax,2
	je	key

	cmp	eax,3
	jne	still
;---------------------------------------------------------------------
align 4
button:
	mcall	17

	cmp	ah, 2
	je	palette_button
	
	cmp	ah, 3
	je	tone_button
	
	cmp	ah, 1
	jne	still

.exit:
	mcall	-1
;---------------------------------------------------------------------
align 4
palette_button:
	mcall	37,1
	and	eax,0xffff
	sub	eax,p_start_y
	imul	eax,p_size_x
	dec	eax
	lea	eax,[eax+eax*2]
	add	eax,[palette_area]
	mov	eax,[eax]
	mov	[tone_color],eax
	mov	[selected_color],eax
	call	create_and_draw_tone
	call	draw_selected_color
	jmp	still
;---------------------------------------------------------------------
align 4
tone_button:
	mcall	37,1
	mov	ebx,eax
	and	eax,0xffff
	shr	ebx,16
	sub	eax,t_start_y
	imul	eax,p_size_y
	sub	ebx,t_start_x
	add	eax,ebx
	lea	eax,[eax+eax*2]
	add	eax,[tone_area]
	mov	eax,[eax]
	mov	[selected_color],eax
	call	draw_selected_color
	jmp	still
;---------------------------------------------------------------------
align 4
key:
	mcall	2
	jmp	still
;---------------------------------------------------------------------
align 4
draw_selected_color:
	mcall	13,<c_start_x,c_size_x>,<c_start_y,c_size_y>,[selected_color]
	ret
;---------------------------------------------------------------------
align 4
create_and_draw_tone:
	call    create_tone
	call    draw_tone
	ret
;---------------------------------------------------------------------
align 4
draw_tone:
	mcall	65,[tone_area],<[tone_SIZE_X],[tone_SIZE_Y]>,<t_start_x,t_start_y>,24
	ret
;---------------------------------------------------------------------
align 4
draw_window:
	mcall	12,1
	mcall	0, <w_start_x,w_size_x>, <w_start_y,w_size_y>, 0x33AABBCC,,title
	mcall	8,<p_start_x,[palette_SIZE_X]>,<p_start_y,[palette_SIZE_Y]>,0x60000002
	mcall	,<t_start_x,[tone_SIZE_X]>,<t_start_y,[tone_SIZE_Y]>,0x60000003
	mcall	65,[palette_area],<[palette_SIZE_X],[palette_SIZE_Y]>,<p_start_x,p_start_y>,24
	call	draw_tone
	call	draw_selected_color
	mcall	12,2
	ret
;---------------------------------------------------------------------
include 'palette.inc'
;---------------------------------------------------------------------
include 'tone.inc'
;---------------------------------------------------------------------
include 'i_data.inc'
;---------------------------------------------------------------------
IM_END:
;---------------------------------------------------------------------
include 'u_data.inc'
;---------------------------------------------------------------------
I_END:
;---------------------------------------------------------------------