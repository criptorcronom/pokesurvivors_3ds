# PokeSurvivors 3DS

Port nativo em C++ para Nintendo 3DS, com libctru, Citro2D/Citro3D e NDSP.

## Compilar no MSYS2 do devkitPro

Requer devkitARM, libctru, citro2d, citro3d, CMake, Make, tex3ds,
3dsxtool e smdhtool. Para CIA, obtenha também o
[makerom oficial](https://github.com/3DSGuy/Project_CTR/releases/tag/makerom-v0.19.0).
No Windows, extraia `makerom.exe` para `tools/local/makerom/`, ou coloque a
ferramenta no PATH. Binários de ferramentas não são enviados ao Git.

Na raiz do projeto, execute:

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
cmake -S . -B build -G "Unix Makefiles"
make -C build -j4
```

Saídas: `build/PokeSurvivors3DS.3dsx`, `build/PokeSurvivors3DS.smdh` e
`build/PokeSurvivors3DS.cia`. Para gerar apenas o instalador e suas dependências:

```bash
make -C build -j4 cia
```

O alvo `cia` existe quando makerom e smdhtool são encontrados na configuração.
Depois de instalar as ferramentas, execute novamente `cmake -S . -B build`.
Também é possível indicar `-DTOOL_MAKEROM=/c/caminho/makerom.exe`.
O RSF em `build_rules/app.rsf` configura a instalação no SD, inclui o RomFS
e permite o serviço DSP. O Title ID local é `000400000F731D00`; mantenha-o
estável nas atualizações e confira conflitos antes de distribuir.

O pacote é homebrew e requer um console com CFW, como Luma3DS, e FBI.
Não inclui banner personalizado para a tela superior do HOME Menu.

## Enviar o código ao GitHub

Destino: https://github.com/criptorcronom/pokesurvivors_3ds

Para a primeira publicação desta pasta, se o remoto estiver vazio:

```bash
git init -b main
git remote add origin https://github.com/criptorcronom/pokesurvivors_3ds.git
git add .gitignore README.md CMakeLists.txt build_rules src include assets
git add tools/bake_audio.py tools/build_textures.py
git add pokesurvivor-main/assets
git diff --cached --stat
git commit -m "Add native 3DS build and CIA packaging"
git push -u origin main
```

Os assets de `pokesurvivor-main/assets` são necessários às ferramentas e ao build.
Confira os arquivos preparados antes do commit. Se `origin` já existir, confira
`git remote -v` e use `git remote set-url origin URL` apenas se precisar corrigi-lo.
Se o remoto já tiver histórico, clone-o em outra pasta, copie os fontes e assets
para esse clone, faça commit e push a partir dele. Preserve o histórico remoto;
não use force push para substituir os commits existentes.

## Criar a Release e obter o link para QR Code

Após publicar o commit correspondente ao binário, use o GitHub CLI:

```bash
gh auth login
git tag v0.1.0
git push origin v0.1.0
gh release create v0.1.0 build/PokeSurvivors3DS.cia build/PokeSurvivors3DS.3dsx \
  --repo criptorcronom/pokesurvivors_3ds --verify-tag \
  --title "PokeSurvivors 3DS v0.1.0" --generate-notes
```

Use uma tag nova para cada versão. Alternativamente, no GitHub, abra
**Releases → Draft a new release**, selecione a tag, anexe o `.cia` da pasta
`build` e publique. O binário é um anexo da Release e está ignorado pelo Git.

Depois de publicar `v0.1.0`, o link direto esperado será:

```text
https://github.com/criptorcronom/pokesurvivors_3ds/releases/download/v0.1.0/PokeSurvivors3DS.cia
```

Para acompanhar a Release estável mais recente, mantendo o mesmo nome do anexo:

```text
https://github.com/criptorcronom/pokesurvivors_3ds/releases/latest/download/PokeSurvivors3DS.cia
```

Codifique a URL direta do `.cia` no QR Code. No FBI, abra
**Remote Install → Scan QR Code**. A Release e o arquivo precisam estar
acessíveis publicamente, sem login. Confira o download numa janela anônima
antes de distribuir o QR. Os links acima só funcionarão após a publicação.

Referências: [criação de Releases pelo CLI](https://cli.github.com/manual/gh_release_create),
[links diretos de anexos](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases),
[documentação do makerom](https://github.com/3DSGuy/Project_CTR/blob/master/makerom/README.md).

## Verificação local

Foram executados o build completo e um build limpo do alvo `cia`.
O `ctrtool` 1.3.0 confirmou os hashes do conteúdo, ExeFS e RomFS e a presença
dos arquivos de áudio e da textura. O relatório local está em
`build/cia-verification.txt`. As falhas de assinatura do ticket, TMD e
AccessDescriptor são esperadas para este pacote assinado com chaves de teste,
sem assinatura oficial Nintendo. Isso requer CFW.

A instalação pelo FBI e a execução em hardware ainda não foram testadas.
O push, a publicação da Release e a criação do QR Code são etapas posteriores.
