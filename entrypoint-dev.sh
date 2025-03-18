#!/bin/sh

if [ ! -f "/etc/procyon/krb5.conf" ]; then
  cp -f /etc/procyon-tmp/krb5.conf /etc/procyon/krb5.conf
fi
rm -f /etc/krb5.conf
ln -s /etc/procyon/krb5.conf /etc/krb5.conf

if [ -f "/etc/procyon/hosts" ]; then
  start_tag="# Procyon Hosts"
  append_mode=false

  # Read the file line by line
  while IFS= read -r line; do
    echo "$line"
    case "$append_mode" in
      true)
        echo "$line" >> /etc/hosts
        echo "appended: $line"
        ;;
      *)
        case "$line" in
          *"$start_tag"*)
            append_mode=true
            echo "$line" >> /etc/hosts
            echo "append_mode=true"
            ;;
        esac
        ;;
    esac
  done < /etc/procyon/hosts
fi

cp -f /etc/hosts /etc/procyon/hosts
nohup sh -c /etc/procyon-tmp/copy_hosts.sh &

openssl genpkey -algorithm RSA -out /opt/guacamole/server.key -pkeyopt rsa_keygen_bits:2048
openssl req -new -x509 -key /opt/guacamole/server.key -out /opt/guacamole/server.crt -days 365 -subj "/C=US/ST=State/L=City/O=Organization/CN=localhost"
#chown guacd:guacd /opt/guacamole/server.crt /opt/guacamole/server.key

ssh-keygen -A
/usr/sbin/sshd -D
