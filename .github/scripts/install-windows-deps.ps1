choco install meson --no-progress

Import-Module $env:ChocolateyInstall\helpers\chocolateyProfile.psm1
refreshenv

meson --version # Verify