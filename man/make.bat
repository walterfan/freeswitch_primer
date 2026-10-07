@ECHO OFF

pushd %~dp0

REM Command file for Sphinx documentation

if "%SPHINXBUILD%" == "" (
	set SPHINXBUILD=sphinx-build
)
set SOURCEDIR=.
set BUILDDIR=_build

%SPHINXBUILD% >NUL 2>NUL
if errorlevel 9009 (
	echo.
	echo.The 'sphinx-build' command was not found. Make sure you have Sphinx
	echo.installed, then set the SPHINXBUILD environment variable to point
	echo.to the full path of the 'sphinx-build' executable. Alternatively you
	echo.may add the Sphinx directory to PATH.
	echo.
	echo.If you don't have Sphinx installed, grab it from
	echo.https://www.sphinx-doc.org/
	exit /b 1
)

if "%1" == "" goto help
if "%1" == "install" goto install
if "%1" == "serve" goto serve
if "%1" == "serve-all" goto serve
if "%1" == "serve-watch" goto serve_watch
if "%1" == "html-en" goto html_en
if "%1" == "html-zh" goto html_zh
if "%1" == "html-all" goto html_all

%SPHINXBUILD% -M %1 %SOURCEDIR% %BUILDDIR% %SPHINXOPTS% %O%
goto end

:install
where poetry >NUL 2>NUL
if not errorlevel 1 (
	poetry install
) else (
	pip install -r requirements.txt
)
goto end

:html_en
if exist %BUILDDIR%\site\en rmdir /s /q %BUILDDIR%\site\en
if not exist %BUILDDIR%\site mkdir %BUILDDIR%\site
set SPHINX_LANGUAGE=en
%SPHINXBUILD% -b html %SOURCEDIR% %BUILDDIR%\site\en %SPHINXOPTS% %O%
goto end

:html_zh
if exist %BUILDDIR%\site\zh rmdir /s /q %BUILDDIR%\site\zh
if not exist %BUILDDIR%\site mkdir %BUILDDIR%\site
sphinx-intl build -d locale -l zh_CN
set SPHINX_LANGUAGE=zh_CN
%SPHINXBUILD% -b html -D language=zh_CN %SOURCEDIR% %BUILDDIR%\site\zh %SPHINXOPTS% %O%
goto end

:html_all
call "%~f0" html-en
if errorlevel 1 goto end
call "%~f0" html-zh
if errorlevel 1 goto end
python scripts\render_landing.py --site-dir %BUILDDIR%\site --default-language en
goto end

:serve
call "%~f0" html-all
if errorlevel 1 goto end
python -m http.server 7008 --directory %BUILDDIR%\site
goto end

:serve_watch
%SPHINXBUILD% -M html %SOURCEDIR% %BUILDDIR% %SPHINXOPTS% %O%
sphinx-autobuild %SOURCEDIR% %BUILDDIR%\html %SPHINXOPTS% %O% --open-browser
goto end

:help
%SPHINXBUILD% -M help %SOURCEDIR% %BUILDDIR% %SPHINXOPTS% %O%

:end
popd
