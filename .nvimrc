set shiftwidth=2
set softtabstop=2
set shiftwidth=2
set tabstop=2

nnoremap <Leader>b :!pushd build && cmake --build . -j10 && popd<CR>

if g:os == "Windows"
  nnoremap <Leader>f :!clang-format -i %:p<CR>
else
  nnoremap <Leader>f :!clang-format-18 -i %:p<CR>
endif
