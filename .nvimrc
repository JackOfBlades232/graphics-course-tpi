set shiftwidth=2
set softtabstop=2
set shiftwidth=2
set tabstop=2

nnoremap <Leader>b :!pushd build && cmake --build . && popd<CR>

if g:os == "Windows"
  nnoremap <Leader>f :!clang-format -i %:p<CR>
else
  nnoremap <Leader>f :!clang-format-21 -i %:p<CR>
endif

command! ClangFormat !python3 clang_format_all.py

if has("nvim")
lua << EOF
    vim.lsp.config("clangd", {
        cmd = {
            "clangd",
            "--compile-commands-dir=build",
            "--background-index",
            "--limit-results=10",
            "--pch-storage=disk"
        }
    })
EOF
endif
