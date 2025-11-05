for D in `ls -d *-*/`; do
    E=$(echo ${D} | sed 's#/##')
    echo "download ${E}.tgz"
    curl -kL https://github.com/PeterSuh-Q3/arpl-modules/releases/latest/download/${E}.tgz -o /tmp/${E}.tgz
    echo "extract ${E} peter's module pack"
    tar -xaf /tmp/${E}.tgz -C ./${E}/
done

