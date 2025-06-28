# Gamepad-Driver
```bash
$ git clone https://github.com/MeHappy3/Gamepad-Driver
$ gcc -o gamepad_driver gamepad_driver.c
$ sudo mv gamepad_driver /usr/local/bin/ 
$ sudo mv gamepad.conf /usr/local/bin/
$ sudo mv gamepad.service /etc/systemd/system/
$ sudo systemctl enable gamepad.service 
$ sudo systemctl start gamepad.service 
```2
