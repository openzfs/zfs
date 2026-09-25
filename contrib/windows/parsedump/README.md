# To extract crash dump for OpenZFS on Windows.

Prepare the required software

* Download and install cdb from Microsoft https://download.microsoft.com/download/7/9/6/7962e9ce-cd69-4574-978c-1202654bd729/windowssdk/Installers/X64%20Debuggers%20And%20Tools-x64_en-us.msi
* Install Python 3 from the Microsoft store https://apps.microsoft.com/store/detail/python-39/9P7QFQMJRFP7

Run the script

* Browse to C:\Program Files\OpenZFS On Windows
* Run parsedump.bat

Upload the results

* Drag and drop C:\stack.txt and C:\cbuf.txt into the editor textbox when creating an issue on GitHub

