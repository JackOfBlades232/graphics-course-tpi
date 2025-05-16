set shiftwidth=2
set softtabstop=2
set shiftwidth=2
set tabstop=2

nnoremap <Leader>b :!pushd build && cmake --build . -j10 && popd<CR>
nnoremap <Leader>f :!clang-format-18 -i %:p<CR>
