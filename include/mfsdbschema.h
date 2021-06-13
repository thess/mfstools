#ifndef MFSDBSCHEMA_H
#define MFSDBSCHEMA_H

struct mfs_db_attribute_schema_s
{
	char *name;
	char type;
	char dependency;
	char level;
	char arity;
};

struct mfs_db_object_schema_s
{
	char *name;
	unsigned char nattributes;
	struct mfs_db_attribute_schema_s *attributes;
};

extern struct mfs_db_object_schema_s mfs_db_schema[];
extern const int mfs_db_schema_nobjects;

#endif
